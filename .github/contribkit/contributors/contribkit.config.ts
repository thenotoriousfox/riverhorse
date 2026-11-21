import { defineConfig, tierPresets } from '@lizardbyte/contribkit'

const riverhorseContributors = new Set((process.env.RIVERHORSE_CONTRIBUTORS ?? '').split(' ').filter(Boolean))

export default defineConfig({
  filter: contributor => riverhorseContributors.has(contributor.sponsor.login),
  tiers: [
    { title: 'Hatchlings', preset: tierPresets.base },
    { title: 'Waders', monthlyDollars: 12, preset: tierPresets.medium },
    { title: 'Bulls', monthlyDollars: 25, preset: tierPresets.large },
    { title: 'Herd Leaders', monthlyDollars: 50, preset: tierPresets.xl },
  ],
})
