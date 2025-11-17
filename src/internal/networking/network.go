package networking

// This file previously contained deprecated global functions and variables.
// All functionality has been moved to:
// - Manager (for applying network configuration)
// - InterfaceSelector (for choosing best interface)
//
// Create instances using:
//   client := keenetic.NewClient(nil)
//   manager := networking.NewManager(client)
//   selector := networking.NewInterfaceSelector(client)
