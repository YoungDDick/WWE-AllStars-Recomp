package main

import (
	"net/netip"
	"testing"

	"github.com/wweallstars-recomp/server/internal/config"
)

// The game reaches the server at the configured public address: the Login reply's secure
// server and the title server list both advertise it.
func TestServersAdvertiseThePublicAddressFromTheConfig(t *testing.T) {
	cfg := config.Config{AccessKey: "k", AuthPort: 32480, HTTPPort: 36000,
		PublicAddress: "46.36.38.4", TrustedProxies: []string{"172.29.48.1"}}
	rv, api := settingsFor(cfg)
	if rv.SecureHost != "46.36.38.4" || rv.SecurePort != 32481 {
		t.Fatalf("secure server %s:%d, want 46.36.38.4:32481", rv.SecureHost, rv.SecurePort)
	}
	if api.RelayAddress != "46.36.38.4:32482" {
		t.Fatalf("relay %q, want 46.36.38.4:32482", api.RelayAddress)
	}
	if api.RendezVousAddress != "46.36.38.4" {
		t.Fatalf("title server %q, want 46.36.38.4", api.RendezVousAddress)
	}
	want := netip.MustParsePrefix("172.29.48.1/32")
	if len(api.TrustedProxies) != 1 || api.TrustedProxies[0] != want {
		t.Fatalf("trusted proxies %v, want [%v]", api.TrustedProxies, want)
	}
}
