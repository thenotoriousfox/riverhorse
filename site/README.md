# riverhorse.app

The website: downloads, documentation, privacy policy and licences. Astro, built to static pages and served by the
`riverhorse-site` Cloudflare Worker (`wrangler.json`). The Docs page is built from sections of the repository's
`README.md`, and the Licences page from `NOTICE.md`, so edit those rather than the site.

    npm install
    npm run dev       # http://localhost:4321
    npm run preview   # build and run the Worker locally
    npm run deploy    # build and publish (needs CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID)

Pushes to `main` that change `site/`, `README.md` or `NOTICE.md` deploy through `.github/workflows/site.yml`.
