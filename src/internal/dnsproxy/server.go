package dnsproxy

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"runtime"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/maksimkurb/keen-pbr/src/internal/log"
	"github.com/maksimkurb/keen-pbr/src/internal/utils"
	"github.com/miekg/dns"
)

type Server struct {
	handler Handler
	config  ProxyConfig
	ctx     context.Context
	cancel  context.CancelFunc
	wg      sync.WaitGroup

	udpConn *net.UDPConn
	tcpLn   net.Listener

	udpRequestChan chan udpRequest
	tcpActiveSem   chan struct{} // Limits concurrent TCP connections

	udpBufPool sync.Pool
	tcpBufPool sync.Pool
}

func NewServer(cfg ProxyConfig, handler Handler) *Server {
	ctx, cancel := context.WithCancel(context.Background())
	return &Server{
		handler: handler, config: cfg, ctx: ctx, cancel: cancel,
		udpBufPool: sync.Pool{New: func() interface{} { b := make([]byte, dns.MaxMsgSize); return &b }},
		tcpBufPool: sync.Pool{New: func() interface{} { b := make([]byte, dns.MaxMsgSize); return &b }},
	}
}

func (s *Server) Start() error {
	addr := fmt.Sprintf("%s:%d", s.config.ListenAddr, s.config.ListenPort)

	// Setup UDP
	uAddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return err
	}
	s.udpConn, err = net.ListenUDP("udp", uAddr)
	if err != nil {
		return err
	}
	setSockOpts(s.udpConn, true)

	// Setup TCP
	s.tcpLn, err = net.Listen("tcp", addr)
	if err != nil {
		utils.CloseOrWarn(s.udpConn)
		return err
	}
	if t, ok := s.tcpLn.(*net.TCPListener); ok {
		setSockOpts(t, false)
	}

	// Calculate Defaults
	workers := s.config.WorkerPoolSize
	if workers <= 0 {
		workers = runtime.NumCPU() * 2
	}

	queueSize := s.config.WorkQueueSize
	if queueSize <= 0 {
		queueSize = workers * 8
	}

	maxTCP := s.config.MaxTCPConnections
	if maxTCP <= 0 {
		maxTCP = 100
	}

	// Initialize Channels
	s.udpRequestChan = make(chan udpRequest, queueSize)
	s.tcpActiveSem = make(chan struct{}, maxTCP)

	// Start Workers & Listeners
	for i := 0; i < workers; i++ {
		s.wg.Add(1)
		go s.udpWorker()
	}

	s.wg.Add(2)
	go s.serveUDP()
	go s.serveTCP()

	log.Infof("DNS server started on %s with %d UDP workers, max %d TCP conns", addr, workers, maxTCP)
	return nil
}

func (s *Server) Stop() error {
	log.Infof("Stopping DNS server...")
	s.cancel() // Signal context cancellation

	// Close listeners immediately to unblock Accept/Read calls
	if s.udpConn != nil {
		utils.CloseOrWarn(s.udpConn)
	}
	if s.tcpLn != nil {
		utils.CloseOrWarn(s.tcpLn)
	}

	if s.udpRequestChan != nil {
		close(s.udpRequestChan) // Signal UDP workers to drain
	}
	s.wg.Wait() // Wait for everyone to finish

	log.Infof("DNS server stopped")
	return nil
}

func (s *Server) serveUDP() {
	defer s.wg.Done()
	for {
		if s.ctx.Err() != nil {
			return
		}

		// Optimization: Don't set deadlines per packet, rely on Close() to unblock ReadFromUDP
		bufPtr := s.udpBufPool.Get().(*[]byte)
		n, addr, err := s.udpConn.ReadFromUDP(*bufPtr)

		if err != nil {
			s.udpBufPool.Put(bufPtr)
			if isClosedError(err) {
				return
			}
			continue
		}

		select {
		// Use clientAddr field name to match types.go definition
		case s.udpRequestChan <- udpRequest{clientAddr: addr, buf: *bufPtr, n: n, conn: s.udpConn}:
		default:
			s.udpBufPool.Put(bufPtr) // Drop if queue full
			log.Warnf("UDP dropped (queue full) from %s", addr)
		}
	}
}

func (s *Server) udpWorker() {
	defer s.wg.Done()
	for req := range s.udpRequestChan {
		bufPtr := &req.buf
		// Use clientAddr field name to match types.go definition
		resp, err := s.handler.HandleRequest(req.clientAddr, req.buf[:req.n], networkUDP)
		s.udpBufPool.Put(bufPtr) // Return to pool immediately

		if err == nil && len(resp) > 0 {
			// Reuse conn from request or s.udpConn
			s.udpConn.WriteToUDP(resp, req.clientAddr)
		}
	}
}

func (s *Server) serveTCP() {
	defer s.wg.Done()
	for {
		conn, err := s.tcpLn.Accept()
		if err != nil {
			if s.ctx.Err() != nil || isClosedError(err) {
				return
			}
			continue
		}

		// Semaphore pattern: Limits concurrency identical to a worker pool
		select {
		case s.tcpActiveSem <- struct{}{}:
			go func() {
				s.handleTCP(conn)
				<-s.tcpActiveSem
			}()
		default:
			utils.CloseOrWarn(conn) // Reject immediately if full
			log.Warnf("TCP rejected (limit reached) from %s", conn.RemoteAddr())
		}
	}
}

func (s *Server) handleTCP(conn net.Conn) {
	defer utils.CloseOrWarn(conn)
	conn.SetDeadline(time.Now().Add(tcpConnectionTimeout))

	var length uint16
	// Use maxDNSMessageSize from types.go (4096)
	if binary.Read(conn, binary.BigEndian, &length) != nil || length > maxDNSMessageSize {
		return
	}

	bufPtr := s.tcpBufPool.Get().(*[]byte)
	defer s.tcpBufPool.Put(bufPtr)

	if _, err := io.ReadFull(conn, (*bufPtr)[:length]); err != nil {
		return
	}

	resp, err := s.handler.HandleRequest(conn.RemoteAddr(), (*bufPtr)[:length], networkTCP)
	if err == nil && len(resp) > 0 {
		conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
		binary.Write(conn, binary.BigEndian, uint16(len(resp)))
		conn.Write(resp)
	}
}

// Helpers

func setSockOpts(c interface{ File() (*os.File, error) }, isUDP bool) {
	f, err := c.File()
	if err != nil {
		return
	}
	defer f.Close()

	fd := int(f.Fd())
	syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
	if isUDP {
		syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_RCVBUF, 8*1024*1024)
	} else {
		syscall.SetsockoptInt(fd, syscall.IPPROTO_TCP, syscall.TCP_NODELAY, 1)
	}
}

func isClosedError(err error) bool {
	if err == nil {
		return false
	}
	if err == io.EOF {
		return true
	}
	// Check for common closed connection error messages
	s := err.Error()
	return strings.Contains(s, "closed") || strings.Contains(s, "use of closed network connection")
}
