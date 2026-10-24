# Security policy

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting for this repository
(Security › Report a vulnerability). Do not open a public issue for security problems.

We aim to acknowledge reports within 72 hours and to agree a disclosure date with you.

## Scope

- Faro code in this repository: `app/`, `console/`, `sim/`, `scripts/`, `web-flasher/`.
- MeshCore itself (`external/MeshCore`) is upstream code; report issues there to
  https://github.com/meshcore-dev/MeshCore unless they are caused by how Faro uses it.

## Known limitations

- The Wi-Fi companion port (TCP 5000) is unauthenticated (upstream MeshCore behaviour).
- Release images are not signed and flash encryption is not enabled; anyone with USB access to a device can
  read its flash, including the node's private key.
