# keyring

`omarchy-pad-keyring`: the public key that Omarchy Pad's packages and its repository databases are signed with, and
pacman's trust in it (`--populate omarchy-pad`), as `omarchy-keyring` does for Omarchy.

| | |
|---|---|
| Key | `Omarchy Pad packages <samyn@mailbox.org>`, ed25519 |
| Fingerprint | `A9ED DCA3 20FC 25A5 EC8B  7F37 9010 4B1A 1337 F4A9` |

The secret key is not here. `bin/publish` signs with it from its own GnuPG home
(`~/.local/share/omarchy-pad-signing/gnupg` on the machine that publishes). A new key, or a revoked one, means a new
`omarchy-pad-keyring` version: `omarchy-pad.gpg` holds the public keys, `omarchy-pad-trusted` the trusted
fingerprints (`FINGERPRINT:4:`), `omarchy-pad-revoked` the revoked ones.
