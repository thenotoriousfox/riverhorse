# riverhorse.app

The website: downloads, documentation, privacy policy and licences. Astro, built to static pages and served by the
`riverhorse-site` Cloudflare Worker (`wrangler.json`). The docs pages are built from sections of the repository's
`README.md` (which section goes on which page is in `src/lib/docs.ts`), the Licences page from `NOTICE.md`, the
Changelog from `CHANGELOG.md` and the Compatibility list from `data/resources/gamedb.yaml`, so edit those rather than
the site. Screenshots are in `public/shots`, and `public/og.png` is the preview image shown when a link is shared.

    npm install
    npm run dev       # http://localhost:4321
    npm run preview   # build and run the Worker locally
    npm run deploy    # build and publish (needs CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID)

Pushes to `main` that change any of those files deploy through `.github/workflows/site.yml`.
