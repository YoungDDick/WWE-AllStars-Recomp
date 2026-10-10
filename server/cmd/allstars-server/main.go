// allstars-server is the WWE All Stars recomp's game server: the Rendez-Vous stand-in the
// game logs in to, the session list (the XWS API) and the relay for peers that cannot reach
// each other directly.
//
//	allstars-server init <game>/Game/Config/Xenon/Cooked/Coalesced.ini
//	allstars-server serve
//	allstars-server health
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/netip"
	"os"
	"time"

	"github.com/wweallstars-recomp/server/internal/config"
	"github.com/wweallstars-recomp/server/internal/relay"
	"github.com/wweallstars-recomp/server/internal/rendezvous"
	"github.com/wweallstars-recomp/server/internal/xws"
)

func usage() {
	fmt.Fprintln(os.Stderr, `usage:
  allstars-server init [-config allstars-server.json] <Coalesced.ini>
      reads the title's [Quazal] access key and port from your own copy of the game
  allstars-server serve [-config allstars-server.json]
  allstars-server health [-config allstars-server.json]
      exits 0 when the XWS API on this machine answers (the container's health check)`)
	os.Exit(2)
}

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	flags := flag.NewFlagSet(os.Args[1], flag.ExitOnError)
	configPath := flags.String("config", config.DefaultPath, "configuration file")
	authOnly := flags.Bool("auth-only", false,
		"developer: no secure server, so scripts/online_listener.py can capture its port")
	flags.Parse(os.Args[2:])
	switch os.Args[1] {
	case "init":
		if flags.NArg() != 1 {
			usage()
		}
		if err := config.Init(flags.Arg(0), *configPath); err != nil {
			log.Fatal(err)
		}
		fmt.Printf("wrote %s\n", *configPath)
	case "serve":
		cfg, err := config.Load(*configPath)
		if err != nil {
			log.Fatal(err)
		}
		serve(cfg, *authOnly)
	case "health":
		cfg, err := config.Load(*configPath)
		if err != nil {
			log.Fatal(err)
		}
		if err := health(cfg); err != nil {
			log.Fatal(err)
		}
	default:
		usage()
	}
}

// settingsFor wires the configuration into both servers: both advertise the public address.
func settingsFor(cfg config.Config) (rendezvous.Settings, xws.Settings) {
	return rendezvous.Settings{AccessKey: cfg.AccessKey, OnCall: logCall,
			Accounts: rendezvous.NewAccounts(), SecureHost: cfg.PublicAddress, SecurePort: cfg.AuthPort + 1},
		xws.Settings{RendezVousAddress: cfg.PublicAddress, TrustedProxies: cfg.TrustedProxyPrefixes(),
			RelayAddress: fmt.Sprintf("%s:%d", cfg.PublicAddress, relayPort(cfg)), Logf: log.Printf,
			RequiredGameVersion: cfg.RequiredGameVersion,
			ProbePlayer: func(to netip.AddrPort) bool { return relay.ProbeDirect(to, 2*time.Second) }}
}

// relayPort follows the secure server's port.
func relayPort(cfg config.Config) int { return cfg.AuthPort + 2 }

// directory gives the relay the XWS API's players and sessions.
type directory struct{ api *xws.Server }

func (d directory) Token(xuid uint64) ([relay.TokenSize]byte, netip.Addr, bool) {
	return d.api.RelayToken(xuid)
}
func (d directory) MayRelay(a, b uint64) bool { return d.api.MayRelay(a, b) }

func serve(cfg config.Config, authOnly bool) {
	settings, apiSettings := settingsFor(cfg)
	api := xws.New(apiSettings)
	go func() { log.Fatal(api.Listen(fmt.Sprintf(":%d", cfg.HTTPPort))) }()
	if !authOnly {
		go rendezvous.NewSecureServer(settings).Listen(settings.SecurePort)
	}
	go func() {
		log.Fatal(relay.New(directory{api}, relay.DefaultLimits, time.Now).Listen(relayPort(cfg)))
	}()
	log.Printf("Rendez-Vous authentication on UDP %d, secure server on UDP %d (%s)", cfg.AuthPort,
		settings.SecurePort, cfg.PublicAddress)
	rendezvous.NewAuthServer(settings).Listen(cfg.AuthPort)
}

// health asks the local XWS API for whoami.
func health(cfg config.Config) error {
	client := http.Client{Timeout: 3 * time.Second}
	response, err := client.Get(fmt.Sprintf("http://127.0.0.1:%d/whoami", cfg.HTTPPort))
	if err != nil {
		return err
	}
	response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("whoami: HTTP %d", response.StatusCode)
	}
	return nil
}

// logCall logs an RMC request, with the username of an Authentication.Login.
func logCall(call rendezvous.Call) {
	if call.Protocol == 0x0A && call.Method == 1 {
		log.Printf("Authentication.Login(%q) call %d", rmcString(call.Parameters), call.CallID)
		return
	}
	log.Printf("RMC request protocol %#02x method %d call %d (%d bytes)", call.Protocol, call.Method,
		call.CallID, len(call.Parameters))
}

// rmcString reads an RMC string: u16 length including the terminating zero, then the bytes.
func rmcString(data []byte) string {
	if len(data) < 2 {
		return ""
	}
	n := int(binary.LittleEndian.Uint16(data))
	if n == 0 || len(data) < 2+n {
		return ""
	}
	return string(data[2 : 2+n-1])
}
