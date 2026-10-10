// Package config holds the server's local configuration. The title's Quazal access key
// comes from the operator's own copy of the game (`allstars-server init`), so it is never
// in Git or in the binary.
package config

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/netip"
	"os"
	"regexp"
	"strconv"
)

// DefaultPath is where init writes the configuration and serve reads it.
const DefaultPath = "allstars-server.json"

// ErrNotInitialized means the configuration file does not exist yet.
var ErrNotInitialized = errors.New("not initialized")

// Config is the server's local configuration.
type Config struct {
	// AccessKey is the title's [Quazal] AccessKey; it seeds the PRUDP checksums.
	AccessKey string `json:"access_key"`
	// AuthPort is the Rendez-Vous authentication port ([Quazal] ServerPort). The secure
	// server listens on the next port.
	AuthPort int `json:"auth_port"`
	// PublicAddress is the IPv4 address players reach the server at; the Login reply gives
	// it as the secure server's address. Loopback when empty.
	PublicAddress string `json:"public_address,omitempty"`
	// HTTPPort serves the XWS API the SDK's online client calls (36000 when zero).
	HTTPPort int `json:"http_port,omitempty"`
	// TrustedProxies are the reverse proxies (addresses or CIDR prefixes) whose
	// X-Forwarded-For the XWS API believes; none by default.
	TrustedProxies []string `json:"trusted_proxies,omitempty"`
	// RequiredGameVersion is the game version players must send ("tu2" from release 1.0.0
	// on); empty takes every client.
	RequiredGameVersion string `json:"required_game_version,omitempty"`
}

// TrustedProxyPrefixes is TrustedProxies as prefixes (a plain address is a /32 or /128);
// Load has rejected entries that are neither.
func (c Config) TrustedProxyPrefixes() []netip.Prefix {
	prefixes, _ := parsePrefixes(c.TrustedProxies)
	return prefixes
}

func parsePrefixes(entries []string) ([]netip.Prefix, error) {
	prefixes := make([]netip.Prefix, 0, len(entries))
	for _, entry := range entries {
		if prefix, err := netip.ParsePrefix(entry); err == nil {
			prefixes = append(prefixes, prefix.Masked())
			continue
		}
		addr, err := netip.ParseAddr(entry)
		if err != nil {
			return nil, fmt.Errorf("trusted proxy %q is not an address or prefix", entry)
		}
		prefixes = append(prefixes, netip.PrefixFrom(addr, addr.BitLen()))
	}
	return prefixes, nil
}

// Lines of a UE3 Coalesced.ini: a commented key starts with ';'.
var keyLine = regexp.MustCompile(`(?m)^[ \t]*(;?)[ \t]*(AccessKey|ServerPort)[ \t]*=[ \t]*([^\r\n\x00]+)`)

// Init reads the access key and port from the game's Coalesced.ini and writes the
// configuration to configPath.
func Init(iniPath, configPath string) error {
	data, err := os.ReadFile(iniPath)
	if err != nil {
		return err
	}
	cfg := Config{AuthPort: 32480}
	for _, m := range keyLine.FindAllSubmatch(data, -1) {
		if len(m[1]) != 0 {
			continue
		}
		value := string(m[3])
		switch string(m[2]) {
		case "AccessKey":
			if cfg.AccessKey == "" {
				cfg.AccessKey = value
			}
		case "ServerPort":
			if port, err := strconv.Atoi(value); err == nil {
				cfg.AuthPort = port
			}
		}
	}
	if cfg.AccessKey == "" {
		return fmt.Errorf("no active [Quazal] AccessKey in %s", iniPath)
	}
	out, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(configPath, append(out, '\n'), 0o600)
}

// Load reads the configuration that Init wrote.
func Load(configPath string) (Config, error) {
	data, err := os.ReadFile(configPath)
	if errors.Is(err, os.ErrNotExist) {
		return Config{}, fmt.Errorf("%w: %s is missing; run `allstars-server init "+
			"<game>/Game/Config/Xenon/Cooked/Coalesced.ini` first", ErrNotInitialized, configPath)
	}
	if err != nil {
		return Config{}, err
	}
	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return Config{}, fmt.Errorf("%s: %w", configPath, err)
	}
	if cfg.AccessKey == "" {
		return Config{}, fmt.Errorf("%w: %s has no access key; run `allstars-server init` again",
			ErrNotInitialized, configPath)
	}
	if cfg.AuthPort == 0 {
		cfg.AuthPort = 32480
	}
	if cfg.HTTPPort == 0 {
		cfg.HTTPPort = 36000
	}
	if cfg.PublicAddress == "" {
		cfg.PublicAddress = "127.0.0.1"
	}
	if _, err := parsePrefixes(cfg.TrustedProxies); err != nil {
		return Config{}, fmt.Errorf("%s: %w", configPath, err)
	}
	return cfg, nil
}
