package models

// GeneralSettings represents general application settings
type GeneralSettings struct {
	DefaultDNSServer    *DNS `json:"defaultDnsServer,omitempty"`
	BootstrapDNSServer  *DNS `json:"bootstrapDnsServer,omitempty"`
}

// Validate checks if general settings are valid
func (s *GeneralSettings) Validate() error {
	if s.DefaultDNSServer != nil {
		if err := s.DefaultDNSServer.Validate(); err != nil {
			return err
		}
	}
	if s.BootstrapDNSServer != nil {
		if err := s.BootstrapDNSServer.Validate(); err != nil {
			return err
		}
	}
	return nil
}
