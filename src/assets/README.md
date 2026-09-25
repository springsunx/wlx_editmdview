# Embedded preview assets

- `highlight.min.js` is the unmodified Highlight.js 11.12.0 common browser build.
- Upstream: `https://github.com/highlightjs/highlight.js/tree/11.12.0`
- Distribution: `https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11.12.0/build/highlight.min.js`
- SHA-256: `8AB71EB09C51F501E5E25157D9CFF100E46CC29BCBFC744D0B746D451FCA7F53`

The following unmodified 11.12.0 language modules supplement the common build:

- CMake: `0CD6DC22B0B99DC53A4288AE30F760DAB4DA12DDDE570F617BE30A396B5B97EC`
- DOS/Batch: `E184A6F9CEAD550B7B39B6114D17CAFD08904557317622D7EA488AED01CF31EE`
- Lisp: `FFDEAB1AFC214245015EF80500E4831712700D98C81C55E391325F39E01EDC98`
- PowerShell: `FC298B3E0DB362E531E6D58988B7A78F83DA19DF6B5D74DB75BC961B2BFBFD3D`
- Properties: `8A987022CC566FA5BFCD79058EC4BA010920D576FFF809B92317295827402590`

The JavaScript and CSS files in this directory are compiled into the WLX binary as
Windows resources. They are not separate runtime files in the release package.
