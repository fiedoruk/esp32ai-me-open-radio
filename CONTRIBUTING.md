# Contributing

[Polska wersja](CONTRIBUTING.pl.md)

The project ships public firmware releases (0.1, 0.2, 0.2.1, 0.4, 0.4.1, current 0.4.2) validated on real
hardware. Contributions are:

- software under GPL-3.0-or-later,
- original documentation/artwork under CC BY-SA 4.0,
- catalog data under CC0-1.0.

By contributing, you confirm that you have the right to submit the material
under the applicable project license.

## Contribution licence

Submitting a contribution also grants the maintainer a royalty-free,
non-exclusive licence with the right to sublicense it, including under terms
other than GPL-3.0-or-later. **You keep your copyright**; nothing already
released stops being GPL. The reason is simple: a project with one maintainer
must not be blocked years later by a single change whose author cannot be found.

The full terms, the fields of exploitation and the opt-out are in
[CONTRIBUTOR-LICENCE.md](CONTRIBUTOR-LICENCE.md). Read it before your first pull
request — it is short, and it is the only document here with legal effect.

## Principles

- Keep the reliable core small.
- Source code, identifiers and technical interfaces use English.
- Public user documentation maintains English and Polish parity.
- Do not add telemetry, cloud dependencies, automatic update channels or remote code execution.
- Do not add official station logos without a rights manifest and documented permission.
- Original station themes are welcome; private assets belong in ignored `assets-local/`.
- Do not add recordings, premium endpoints or unofficial stream proxies.
- Every station endpoint needs an official source page and verification date.

## Pull requests

1. Open or reference an issue/RFC for material changes.
2. Keep one concern per pull request.
3. Include focused tests and hardware evidence when applicable.
4. Update EN/PL user documentation together.
5. Sign commits with the DCO trailer: `Signed-off-by: Name <email>`.

Network, security, USB-rescue and stable-catalog changes require two reviews. Other
changes require one maintainer review. The project's validation gates run in the
maintainer's own workspace rather than in this repository, so every contribution
is reviewed and validated by the maintainer before merge.

## Station contributions

Provide the operator, official player, resolver source, codec, transport,
region, verification date and Core2 test result. `Verified` means technically
tested, not approved or endorsed by the broadcaster.

## Community evidence

Send compatibility evidence as structured JSON. A report must not contain
endpoint URLs, SSIDs, credentials, Bluetooth addresses, device serials, listener
identity, PCM or artwork. The maintainer validates every report before it is
used. A `PASS` result is software evidence for the declared firmware hash; it is
not a general hardware-compatibility claim.

See `REPRODUCIBILITY.md` for the complete source-candidate workflow.
