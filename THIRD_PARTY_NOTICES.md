# Third-party software and services

StarryFish's MIT license applies to its original host, installer and adapters.
It does not relicense the independent extensions, scripts, translation data,
Microsoft runtime, or Granblue Fantasy assets. StarryFish is not affiliated
with Cygames, Mobage, Microsoft, or the authors of these components.

## Included scripts

- **gbf-bookmark 0.2.8**, biuuu: https://github.com/biuuu/gbf-bookmark — MIT,
  full notice in `Licenses/gbf-bookmark-MIT.txt` (source: `licenses/`).
- **BLHXFY 2.9.5**, biuuu: https://github.com/biuuu/BLHXFY — script code MIT,
  full notice in `Licenses/BLHXFY-MIT.txt`. Embedded third-party notices
  (including URI.js) remain in the original script. Translation data has a
  separate upstream license and is fetched from upstream; it is not included
  in this repository or installer. Script download metadata disables automatic
  script replacement; host adapters are separate source files.

## Installer payload

- **ZeroOmega 3.5.2**, zero-peak / SwitchyOmega contributors:
  https://github.com/zero-peak/ZeroOmega/tree/v3.5.2 — GPL-3.0.
  This is a separate, unmodified browser extension, not linked into the host.
  Its complete package, `COPYING`, `AUTHORS`, dependency notices and license
  headers are preserved. The matching upstream source archive is provided
  alongside the installer in each Release (`ZeroOmega-v3.5.2-source.zip`);
  build instructions and dependency manifests are in that archive. See also
  `Licenses/ZeroOmega-third-party.txt`.
- **Microsoft WebView2 SDK 1.0.4258.31**, official NuGet package:
  https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31 .
  The loader is linked under the included `WebView2-SDK-LICENSE.txt`.
  The Evergreen Runtime is a separately installed Microsoft product and is
  not bundled. No Microsoft SDK binaries are committed to this repository.

## Downloaded directly on the user's computer

- **Tarou**: https://github.com/Waaatanuki/Chrome-Extension-Tarou
- **arcarumDungeon**: https://github.com/Aureth/arcarumDungeon

These packages are not mirrored in this repository or installer. The installer
downloads the maintainer's public Release; arcarumDungeon may instead use a
resolved public development commit ZIP when no installable Release exists.
Original files and source records are retained locally. StarryFish claims no
ownership or MIT rights over these projects or their game artwork. Their use
and services remain subject to their upstream terms. Installing StarryFish
with its documented helpers initiates these downloads.

## Artwork

The StarryFish fish icon was generated with OpenAI image generation for this
project and selected by the maintainer. It is not game artwork. No game
screenshots, character illustrations or account screenshots are shipped.
