package models

// GeneralSettings represents general application settings
type GeneralSettings struct {
	DefaultDNSServer   *DNS   `json:"defaultDnsServer,omitempty"`
	BootstrapDNSServer *DNS   `json:"bootstrapDnsServer,omitempty"`
	IPDomain           string `json:"ipDomain,omitempty"`     // Domain for checking real IP (default: "ip.podkop.fyi")
	FakeIPDomain       string `json:"fakeipDomain,omitempty"` // Domain for fakeip testing (default: "fakeip.podkop.fyi")
	SingBoxVersion     string `json:"singBoxVersion,omitempty"` // sing-box version to use (default: "1.12.12")
	SingBoxPath        string `json:"singBoxPath,omitempty"`    // Path to sing-box binary (default: "/usr/local/bin/sing-box")
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
