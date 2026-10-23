# game data

the client reads these json files through `src/common.cpp`. they are hosted at
`https://assets.prisontest.com/data/` and downloaded into the launch cache.
they are not stored in git or included in packages. use `--assets /path/to/asset-pack`
to preview a local copy before publishing changes.

| file | contents |
| --- | --- |
| `balance.json` | currency caps, reward rates, fortune limits, prestige scaling and mine reset rules |
| `ranks.json` | the A-Z rank order and base purchase costs |
| `blocks.json` | legacy block ids and fallback money/EXP rewards; retained for older items and tests |
| `mine.json` | mine bounds, spawn, refill timing and the 26 smooth/ore block pairs with source texture expressions |
| `native_economy.json` | native-only currency labels, enchant price overrides, gem/fuel chances and robot progression |
| `native_content.json` | native-only guardians, crystal resonance and pet mutations |
| `enchants.json` | enchant order, labels, colors, prices, caps, progression scaling and proc parameters |
| `crystals.json` | the fourteen pickaxe levels that unlock crystal slots |
| `source_systems.json` | pouches, autominer upgrades, pet tiers, milestones, salvage, lucky drops, book eligibility and native progression |
| `help.json` | short help pages, search keywords, command categories, descriptions, syntax, aliases and developer-command visibility |

keep ids stable: saves and equipped item metadata refer to them. changing a
price or boost affects existing worlds when loaded; it does not rewrite their
items. rank order, paired mine blocks, pouch capacities and crystal slot count
also have matching limits in code. changing those structures requires updating
their consumers and migrations, not just editing a number.

`source_systems.json` contains native-only `vault` prices: `page_base_cost` is
the second page's price, and `page_cost_multiplier` scales each later purchase.
the first extra page costs 100 trillion money; each subsequent page costs six
times more. all 24 purchases stay below the currency cap without flattening.
vault pages contain 54 slots, capped at 25 pages in the native code. already
purchased pages retain their names and items.

the native-only `discoveries` extension defines per-refill pocket chance, pocket
size/color and one theme name per rank. the renderer reserves one atlas tile for
pocket ore. actual digs drop ordinary unidentified crystals through automatic
item delivery. caves and buried crates are no longer generated.

native progression uses `regions` and `mine_upgrades`. regions specify rebirth
gates, ore offsets/layouts and theme colors. upgrade level arrays define caps,
prices and rebirth gates; ore density adds two percentage points per level and
pockets add one block. ore blocks pay tokens rather than money, so density changes
that mix. prices stay fixed per tier, with no current-prestige multiplier.
owned levels survive price changes.

`part_specializations` retains additive money/token/EXP/egg modifiers for already
owned parts. no forge recipes remain. queued rewards keep their captured bonuses.
on save load, stored forge items return without filtering; full inventory and
vaults retain pending items until room is available. retired materials convert
to points and crate upgrades refund their historical costs once. existing item
metadata, excavations and non-retired upgrades remain intact.

`native_content.json` defines guardian spawn chance, pickaxe gate, health,
duration, cooldown and point rewards. guardian parts from older saves keep their
rolled boost. bosses remain separate from block rewards and enchant procs.

resonance definitions match the first three identified crystals of one tier,
at or above `minimum_tier`. only one set applies: matching sell/token/EXP gives
4% in that category, or one of each gives 2% in every category. additional slots
keep their ordinary boosts. resonance is derived from equipment, not saved.

mutations roll once at hatch completion with probability `chance`. saved pet
metadata retains the trait and roll marker; merging inherits a valid parent
trait or rolls once when none exists. only the active pet grants the configured
secondary bonus. robot savings use integer hundredths in `fuel_credit`, so
batching cannot change consumption and empty tanks grant no boost. pending
rewards retain the original paid robot and equipment multipliers.

block/ore contracts and expeditions credit proc mining using the existing
compressed count, spread across queued payouts. manual mining, bomb excavation
and autominer ticks count directly. this limits side rewards without changing
core enchant money/EXP, hatchery or drop formulas.

native `shaft` defines its prestige requirement, block-target progression and
four floor conditions. each doubling of depth adds one target/difficulty band;
targets cap at the smaller of `max_blocks` and half the mine volume.
the depth multiplier caps at 2x, on top of the condition's dig scale. ordinary
floors award `clear_points`; every `milestone_interval` floors doubles it. accepted
floors keep their saved terms. new depths continue without increasing terrain size. the active and return mines are stored
in the world save, not in a growing list of previous floors.

native `contracts` defines the rotating goals, base targets and point rewards.
three consecutive entries are offered; claiming advances the board. all targets
and point rewards scale by `1 + min(rebirth, 10)`. accepted terms stay in the save.

native `expeditions` defines up to three concurrent trips and routes keyed by
region. a matching pet species uses its unlocked route, otherwise the quarry.
level reduces the route's block target by 0.2% per level, capped at level 75;
rarity adds two points per tier above common. each route also supplies the
listed food or egg stack. pet payloads, terms and progress are saved; these are
mining counters rather than offline timers.

run the static audit after editing:

```sh
python3 tools/check_data.py /path/to/asset-pack
ctest --test-dir build --output-on-failure
```

the audit rejects duplicate keys/ids, non-finite numbers, broken rank/block
pairing, invalid costs/colors/unlocks, missing terrain textures and malformed
help entries. it checks the current structure; gameplay formula and transfer
behavior remain covered by the local C++ tests. the github workflow only builds
and packages the client.

the cached `help.json` works offline. the optional help API can update articles
and descriptions only. the native client retains local command categories, syntax, aliases
and developer visibility, including when an API response omits some commands.
the help API cannot change balance, item stats or command implementations.
the three native progression/economy tables require `data_revision: 2` together.
older or mixed packs are rejected. the client keeps the previous valid cache;
a first launch requires a complete valid download. normal asset updates can
replace the gameplay tables; see [asset hosting](../docs/ASSETS.md).

## native gem economy

only the native loader reads `native_economy.json`. it merges `defs` into the
base enchant definitions, appends `order`, adds `effects`, and exposes
`currencies`/`robots`/`endgame` through the existing systems table. new gem wallets default to zero; existing enchant levels survive.

native prices use `ceil((base_cost + current_level * step) * late_scale)` per
level. `late_cost_scaling` applies to both native and ordinary enchants; its
multiplier is 1 below its start level. gem enchants start scaling at level 100
with power 2; Time Finder starts at 10 because its cap is 100. ruby base/step prices were raised 1,000x and sapphires 2,000x;
token-paid RubyGreed and gem drop amounts/chances are unchanged. keep these
tables in sync on the asset host: cached remote files also supply live prices.
robot upgrade prices still use their separate `upgrade_cost_multiplier`.
on load, saves without `gem_currency_version: 1` convert existing gem balances
at 1:1m, then record that version. both wallets use the ordinary currency cap
instead of the smaller progression-counter cap. purchased levels and other
currencies do not change.
RubyGreed is token-paid, SapphireGreed is ruby-paid, and gems are rolled only
for manual blocks. jackpot weights sum to 100; its drops use current crystal
and part tiers. FuelFinder and robot discovery also use manual blocks only.
premium drop caps are 2.5% for rubies, 0.5% for sapphires and 0.2% for fuel.
their per-level chances fall by the same factors, making them 10x, 20x and 40x
rarer than the previous pass. robots use `upgrade_cost_multiplier` on both gem
prices after summing every purchased level, including sapphire gate crossings.
robot fuel and boost settings are defined in the `robots` table; cached help
keeps the short explanation. fuel items still supply 250
units and loaded fuel is not rescaled. tables contain no executable logic.

`currencies.*.color` supplies scoreboard values, chat amounts and menu costs.
tokens use gold (`#ffcc44`); Token Greed lore and TokenBoost robots use the same
palette entry. item tiers, rename tokens and custom enchant colours remain separate.

legacy vaults without `vault_page_size` use 27-slot page boundaries. the native
loader expands each page to 54 slots, preserving page names and item metadata,
and writes `vault_page_size: 54` to avoid expanding again on later loads.

`endgame` lists the native enchants with manual-only rare rolls and saved
remaining timers. their effects define chances, cooldowns, durations and bounded
boost factors. Jackpot uses this path too. Energizer's allowlist deliberately
excludes itself, Rewind, Jackpot and Credit Finder; changes cannot enable loops.
Rewind's eight-second income window is temporary and never records its own bonus.
