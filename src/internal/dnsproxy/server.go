package dnsproxy

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/miekg/dns"
)

// Server is a generic DNS server that handles network I/O and concurrency.
// It delegates the processing of DNS requests to a Handler.
// This component has NO knowledge of business logic (caching, ipsets, etc.).
type Server struct {
	handler Handler     // The business logic handler
	config  ProxyConfig // Server configuration

	// Lifecycle
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup

	// Listeners
	udpConn *net.UDPConn
	tcpLn   net.Listener

	// Worker pools for bounded concurrency
	udpRequestChan chan udpRequest
	tcpRequestChan chan tcpRequest
	udpWorkerWg    sync.WaitGroup
	tcpWorkerWg    sync.WaitGroup
	tcpActiveSem   chan struct{} // Semaphore for limiting concurrent TCP connections

	// Buffer pools for zero-allocation I/O
	udpReadBufferPool sync.Pool // UDP read buffers (4096 bytes)
	tcpReadBufferPool sync.Pool // TCP read buffers (4096 bytes)
}

// NewServer creates a new DNS server.
func NewServer(cfg ProxyConfig, handler Handler) *Server {
	ctx, cancel := context.WithCancel(context.Background())
	return &Server{
		handler: handler,
		config:  cfg,
		ctx:     ctx,
		cancel:  cancel,
		udpReadBufferPool: sync.Pool{
			New: func() interface{} {
				buf := make([]byte, dns.MaxMsgSize)
				return &buf
			},
		},
		tcpReadBufferPool: sync.Pool{
			New: func() interface{} {
				buf := make([]byte, dns.MaxMsgSize)
				return &buf
			},
		},
	}
}

// Start starts the DNS server listeners and worker pools.
func (s *Server) Start() error {
	// Use configured listen address (default: [::]:port for dual-stack)
	listenAddr := fmt.Sprintf("%s:%d", s.config.ListenAddr, s.config.ListenPort)

	udpAddr, err := net.ResolveUDPAddr("udp", listenAddr)
	if err != nil {
		return fmt.Errorf("failed to resolve UDP address: %w", err)
	}

	s.udpConn, err = net.ListenUDP("udp", udpAddr)
	if err != nil {
		return fmt.Errorf("failed to listen UDP: %w", err)
	}

	// Configure UDP socket options for better performance
	if udpFile, err := s.udpConn.File(); err == nil {
		fd := int(udpFile.Fd())
		// Increase receive buffer to handle bursts (8MB)
		_ = syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_RCVBUF, 8*1024*1024)
		// Enable address reuse for fast restart
		_ = syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
		utils.CloseOrWarn(udpFile)
	}

	s.tcpLn, err = net.Listen("tcp", listenAddr)
	if err != nil {
		utils.CloseOrWarn(s.udpConn)
		return fmt.Errorf("failed to listen TCP: %w", err)
	}

	// Configure TCP socket options for better performance
	if tcpLn, ok := s.tcpLn.(*net.TCPListener); ok {
		if tcpFile, err := tcpLn.File(); err == nil {
			fd := int(tcpFile.Fd())
			// Enable address reuse
			_ = syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
			// Disable Nagle's algorithm for lower latency
			_ = syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, syscall.TCP_NODELAY, 1)
			utils.CloseOrWarn(tcpFile)
		}
	}

	// Initialize worker pool configuration with sensible defaults
	udpWorkerCount := s.config.WorkerPoolSize
	if udpWorkerCount <= 0 {
		udpWorkerCount = runtime.NumCPU() * 2 // I/O-bound, use 2x CPU cores
	}

	tcpWorkerCount := s.config.TCPWorkerPoolSize
	if tcpWorkerCount <= 0 {
		tcpWorkerCount = runtime.NumCPU() // TCP has persistent connections, needs fewer workers
	}

	udpQueueSize := s.config.WorkQueueSize
	if udpQueueSize <= 0 {
		udpQueueSize = udpWorkerCount * 8 // 8 requests per worker
	}

	tcpQueueSize := s.config.TCPQueueSize
	if tcpQueueSize <= 0 {
		tcpQueueSize = tcpWorkerCount * 4 // 4 requests per worker
	}

	maxTCPConns := s.config.MaxTCPConnections
	if maxTCPConns <= 0 {
		maxTCPConns = 100 // Default limit
	}

	// Create worker channels
	s.udpRequestChan = make(chan udpRequest, udpQueueSize)
	s.tcpRequestChan = make(chan tcpRequest, tcpQueueSize)
	s.tcpActiveSem = make(chan struct{}, maxTCPConns)

	// Start UDP worker pool
	s.udpWorkerWg.Add(udpWorkerCount)
	for i := 0; i < udpWorkerCount; i++ {
		go s.udpWorker(i)
	}

	// Start TCP worker pool
	s.tcpWorkerWg.Add(tcpWorkerCount)
	for i := 0; i < tcpWorkerCount; i++ {
		go s.tcpWorker(i)
	}

	log.Infof("DNS server started on %s (UDP/TCP) with %d UDP workers, %d TCP workers",
		listenAddr, udpWorkerCount, tcpWorkerCount)

	// Start listener goroutines
	s.wg.Add(2)
	go s.serveUDP(s.udpConn)
	go s.serveTCP(s.tcpLn)

	return nil
}

// Stop stops the DNS server.
func (s *Server) Stop() error {
	log.Infof("Stopping DNS server...")

	// Cancel context first to signal all goroutines to stop
	s.cancel()

	// Wait for listener goroutines to finish (they will exit when context is cancelled and deadline expires)
	// We DON'T close sockets here because that can cause deadlocks with concurrent SetReadDeadline/SetDeadline calls
	// Since we use 100ms deadlines, goroutines should exit within 200-300ms of context cancellation
	done := make(chan struct{})
	go func() {
		s.wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		// Listeners exited cleanly
	case <-time.After(500 * time.Millisecond): // 5x the deadline to handle load
		log.Warnf("Listener goroutines did not exit within timeout, forcing shutdown")
	}

	// Close sockets in background goroutine to avoid blocking Stop()
	// This ensures ports are released even if Close() blocks temporarily
	socketsClosed := make(chan struct{})
	go func() {
		if s.udpConn != nil {
			utils.CloseOrWarn(s.udpConn)
		}
		if s.tcpLn != nil {
			utils.CloseOrWarn(s.tcpLn)
		}
		close(socketsClosed)
	}()

	// Wait for socket close to complete (with timeout)
	select {
	case <-socketsClosed:
		// Sockets closed successfully
	case <-time.After(200 * time.Millisecond):
		log.Warnf("Socket close did not complete within timeout")
	}

	// Close worker channels to signal workers to stop
	// Workers will process any remaining items in the queue then exit
	if s.udpRequestChan != nil {
		close(s.udpRequestChan)
	}
	if s.tcpRequestChan != nil {
		close(s.tcpRequestChan)
	}

	// Wait for all workers to finish processing (they exit when channel is closed)
	s.udpWorkerWg.Wait()
	s.tcpWorkerWg.Wait()

	log.Infof("DNS server stopped")
	return nil
}

// serveUDP handles incoming UDP DNS queries.
func (s *Server) serveUDP(conn *net.UDPConn) {
	defer s.wg.Done()

	for {
		select {
		case <-s.ctx.Done():
			return
		default:
		}

		// Get buffer from pool
		bufPtr := s.udpReadBufferPool.Get().(*[]byte)
		buf := *bufPtr

		// Set a short deadline so we can check context frequently
		if err := conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond)); err != nil {
			s.udpReadBufferPool.Put(bufPtr)
			if s.ctx.Err() != nil {
				return
			}
			continue
		}
		n, clientAddr, err := conn.ReadFromUDP(buf)
		if err != nil {
			s.udpReadBufferPool.Put(bufPtr)
			// Check if connection was closed (happens during shutdown)
			if s.ctx.Err() != nil {
				return
			}
			// Check for timeout - expected due to short deadline
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			// Any other error (including closed connection) - check if we're shutting down
			if strings.Contains(err.Error(), "closed") || strings.Contains(err.Error(), "use of closed network connection") {
				return
			}
			// Log unexpected errors
			if log.IsVerbose() {
				log.Debugf("UDP read error: %v", err)
			}
			continue
		}

		// Create request and enqueue for worker processing (no copy, transfer ownership)
		req := udpRequest{
			conn:       conn,
			clientAddr: clientAddr,
			buf:        buf,
			n:          n,
		}

		// Non-blocking send to worker channel (backpressure: drop if queue full)
		select {
		case s.udpRequestChan <- req:
			// Successfully enqueued, worker will handle and return buffer to pool
		default:
			// Queue full, drop request and return buffer to pool immediately
			s.udpReadBufferPool.Put(bufPtr)
			if log.IsVerbose() {
				log.Warnf("UDP request dropped (queue full) from %s", clientAddr)
			}
		}
	}
}

// serveTCP handles incoming TCP DNS queries.
func (s *Server) serveTCP(ln net.Listener) {
	defer s.wg.Done()

	// Set accept deadline to allow context checking
	tcpLn := ln.(*net.TCPListener)

	for {
		select {
		case <-s.ctx.Done():
			return
		default:
		}

		// Set a short deadline so we can check context frequently
		if err := tcpLn.SetDeadline(time.Now().Add(100 * time.Millisecond)); err != nil {
			if s.ctx.Err() != nil {
				return
			}
			continue
		}

		conn, err := ln.Accept()
		if err != nil {
			// Check if we're shutting down
			if s.ctx.Err() != nil {
				return
			}
			// Check for timeout - expected due to short deadline
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			// Check for closed listener error
			if strings.Contains(err.Error(), "closed") || strings.Contains(err.Error(), "use of closed network connection") {
				return
			}
			// Log unexpected errors
			if log.IsVerbose() {
				log.Debugf("TCP accept error: %v", err)
			}
			continue
		}

		// Acquire semaphore slot to limit concurrent TCP connections
		select {
		case s.tcpActiveSem <- struct{}{}:
			// Got slot, enqueue for worker processing
			req := tcpRequest{conn: conn}
			select {
			case s.tcpRequestChan <- req:
				// Successfully enqueued, worker will handle and release semaphore
			default:
				// Queue full, close connection and release semaphore
				<-s.tcpActiveSem
				utils.CloseOrWarn(conn)
				log.Warnf("TCP connection dropped (queue full) from %s", conn.RemoteAddr())
			}
		default:
			// Too many active connections, reject immediately
			utils.CloseOrWarn(conn)
			if log.IsVerbose() {
				log.Warnf("TCP connection rejected (too many active) from %s", conn.RemoteAddr())
			}
		}
	}
}

// handleTCPConnection handles a single TCP DNS connection.
func (s *Server) handleTCPConnection(conn net.Conn) {
	defer utils.CloseOrWarn(conn)

	if err := conn.SetDeadline(time.Now().Add(tcpConnectionTimeout)); err != nil {
		log.Debugf("TCP set deadline error: %v", err)
		return
	}

	// Read length prefix
	var length uint16
	if err := binary.Read(conn, binary.BigEndian, &length); err != nil {
		log.Debugf("TCP read length error: %v", err)
		return
	}

	// Validate message size to prevent DoS
	if length == 0 || length > maxDNSMessageSize {
		log.Warnf("Invalid TCP DNS message length: %d from %s", length, conn.RemoteAddr())
		return
	}

	// Get buffer from pool
	bufPtr := s.tcpReadBufferPool.Get().(*[]byte)
	buf := *bufPtr
	defer s.tcpReadBufferPool.Put(bufPtr)

	// Read DNS message using io.ReadFull to guarantee complete read (fixes partial read bug)
	if _, err := io.ReadFull(conn, buf[:length]); err != nil {
		log.Debugf("TCP read message error: %v", err)
		return
	}

	// *** DELEGATE to the handler instead of processing internally ***
	resp, err := s.handler.HandleRequest(conn.RemoteAddr(), buf[:length], networkTCP)
	if err != nil {
		log.Debugf("TCP request processing error: %v", err)
		return
	}

	// Set write deadline for response
	if err := conn.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		log.Debugf("TCP set write deadline error: %v", err)
	}

	// Write length prefix
	if err := binary.Write(conn, binary.BigEndian, uint16(len(resp))); err != nil {
		log.Debugf("TCP write length error: %v", err)
		return
	}

	// Write response
	if _, err := conn.Write(resp); err != nil {
		log.Debugf("TCP write response error: %v", err)
	}
}

// udpWorker processes UDP DNS requests from the worker channel.
func (s *Server) udpWorker(id int) {
	defer s.udpWorkerWg.Done()

	for {
		// Don't use select with ctx.Done() here - let the channel closure signal exit
		// This ensures workers drain the queue before exiting
		req, ok := <-s.udpRequestChan
		if !ok {
			// Channel closed, worker should exit
			return
		}

		// Check if context is cancelled before processing (skip work if shutting down)
		if s.ctx.Err() != nil {
			// Return buffer to pool and skip processing
			bufPtr := &req.buf
			s.udpReadBufferPool.Put(bufPtr)
			continue
		}

		// Process request and always return buffer to pool
		func() {
			bufPtr := &req.buf
			defer s.udpReadBufferPool.Put(bufPtr)

			// *** DELEGATE to the handler instead of processing internally ***
			resp, err := s.handler.HandleRequest(req.clientAddr, req.buf[:req.n], networkUDP)
			if err != nil {
				if log.IsVerbose() {
					log.Debugf("UDP worker %d: handler error: %v", id, err)
				}
				return
			}

			// Send response
			_, err = req.conn.WriteToUDP(resp, req.clientAddr)
			if err != nil {
				log.Warnf("UDP worker %d: write error to %s: %v", id, req.clientAddr, err)
			}
		}()
	}
}

// tcpWorker processes TCP DNS connections from the worker channel.
func (s *Server) tcpWorker(_ int) {
	defer s.tcpWorkerWg.Done()

	for {
		// Don't use select with ctx.Done() here - let the channel closure signal exit
		// This ensures workers drain the queue before exiting
		req, ok := <-s.tcpRequestChan
		if !ok {
			// Channel closed, worker should exit
			return
		}

		// Check if context is cancelled before processing (skip work if shutting down)
		if s.ctx.Err() != nil {
			// Close connection and release semaphore
			utils.CloseOrWarn(req.conn)
			<-s.tcpActiveSem
			continue
		}

		// Handle connection and always release semaphore slot when done
		func() {
			defer func() { <-s.tcpActiveSem }()
			s.handleTCPConnection(req.conn)
		}()
	}
}
