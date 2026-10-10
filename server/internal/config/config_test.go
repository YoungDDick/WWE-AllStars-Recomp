package config

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A UE3 Coalesced.ini holds several inis back to back with binary length prefixes; the
// [Quazal] section of the engine ini has a commented-out key and the active one.
func writeIni(t *testing.T, dir string) string {
	t.Helper()
	text := "\x00\x00\x00\x10../Config/Game.ini\x00[Engine]\r\nFoo=1\r\n" +
		"\x00\x00\x00\x12../Config/Engine.ini\x00[Quazal]\r\n;AccessKey=oldkey\r\n" +
		"AccessKey=t3stk3y\r\nServerPort=32480\r\n\x00"
	path := filepath.Join(dir, "Coalesced.ini")
	if err := os.WriteFile(path, []byte(text), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestInitReadsTheActiveAccessKey(t *testing.T) {
	dir := t.TempDir()
	configPath := filepath.Join(dir, "allstars-server.json")
	if err := Init(writeIni(t, dir), configPath); err != nil {
		t.Fatal(err)
	}
	cfg, err := Load(configPath)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.AccessKey != "t3stk3y" {
		t.Fatalf("access key %q, want t3stk3y", cfg.AccessKey)
	}
}

func TestInitFailsWithoutAnActiveKey(t *testing.T) {
	dir := t.TempDir()
	ini := filepath.Join(dir, "Coalesced.ini")
	os.WriteFile(ini, []byte("[Quazal]\r\n;AccessKey=commented\r\n"), 0o600)
	if err := Init(ini, filepath.Join(dir, "c.json")); err == nil {
		t.Fatal("init succeeded on an ini without an active AccessKey")
	}
}

func TestLoadWithoutInitSaysHowToRunIt(t *testing.T) {
	_, err := Load(filepath.Join(t.TempDir(), "allstars-server.json"))
	if !errors.Is(err, ErrNotInitialized) {
		t.Fatalf("error %v, want ErrNotInitialized", err)
	}
	if !strings.Contains(err.Error(), "allstars-server init") {
		t.Fatalf("error %q does not say how to run init", err)
	}
}

func writeConfig(t *testing.T, text string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "allstars-server.json")
	if err := os.WriteFile(path, []byte(text), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestLoadReadsTheTrustedProxies(t *testing.T) {
	cfg, err := Load(writeConfig(t, `{"access_key":"k","public_address":"46.36.38.4",`+
		`"trusted_proxies":["172.29.48.1","10.1.0.0/16"]}`))
	if err != nil {
		t.Fatal(err)
	}
	got := cfg.TrustedProxyPrefixes()
	if len(got) != 2 || got[0].String() != "172.29.48.1/32" || got[1].String() != "10.1.0.0/16" {
		t.Fatalf("trusted proxies %v, want [172.29.48.1/32 10.1.0.0/16]", got)
	}
	if cfg.PublicAddress != "46.36.38.4" {
		t.Fatalf("public address %q, want 46.36.38.4", cfg.PublicAddress)
	}
}

func TestLoadReadsTheRequiredGameVersion(t *testing.T) {
	cfg, err := Load(writeConfig(t, `{"access_key":"k","required_game_version":"tu2"}`))
	if err != nil || cfg.RequiredGameVersion != "tu2" {
		t.Fatalf("config %+v, err %v", cfg, err)
	}
}

func TestLoadRejectsABadTrustedProxy(t *testing.T) {
	if _, err := Load(writeConfig(t, `{"access_key":"k","trusted_proxies":["nginx"]}`)); err == nil {
		t.Fatal("load accepted a trusted proxy that is not an address")
	}
}
