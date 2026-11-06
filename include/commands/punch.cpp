#include "pch.hpp"

#include "onVariant/SetClothing.hpp"

#include <algorithm>

#include "punch.hpp"

/*
* @note the punch effect each item gives while worn (byte 1 of SET_CHARACTER_STATE), sorted by item id (binary searched).
* @note merged from GrowServer, GrowJS, GTPS3 and VallenGTPS (majority vote when they disagree), only clothing and auras.
*/
static constexpr std::pair<u_short, u_char> punch_ids[] {
    { 138, 1 }, // Cyclopean Visor
    { 366, 2 }, // Heartbow
    { 472, 3 }, // Tommygun
    { 594, 4 }, // Elvish Longbow
    { 768, 5 }, // Sawed-Off Shotgun
    { 900, 6 }, // Dragon Hand
    { 910, 7 }, // Reanimator Remote
    { 930, 8 }, // Death Ray
    { 1010, 8 }, // Destructo Ray
    { 1016, 9 }, // Six Shooter
    { 1204, 10 }, // Focused Eyes
    { 1228, 6 }, // Green Dragon Mask
    { 1250, 7 }, // Bride Of Reanimator Remote
    { 1252, 6 }, // Magnifying Glass
    { 1254, 6 }, // Retro Magnifying Glass
    { 1378, 11 }, // Ice Dragon Hand
    { 1440, 12 }, // Evil Space Helmet
    { 1464, 2 }, // Golden Heartbow
    { 1484, 13 }, // Atomic Shadow Scythe
    { 1512, 14 }, // Pet Leprechaun
    { 1542, 15 }, // Battle Trout
    { 1570, 18 }, // Mariachi Guitar
    { 1576, 16 }, // Fiesta Dragon
    { 1648, 14 }, // Unicorn Garland
    { 1676, 17 }, // Squirt Gun
    { 1710, 18 }, // Keytar
    { 1712, 18 }, // Bass Guitar
    { 1714, 18 }, // Tambourine
    { 1748, 19 }, // Flamethrower
    { 1780, 20 }, // Legendbot-009
    { 1782, 21 }, // Dragon of Legend
    { 1804, 22 }, // Zeus' Lightning Bolt
    { 1868, 23 }, // Violet Protodrake Leash
    { 1874, 24 }, // Ring Of Force
    { 1946, 25 }, // Ice Calf Leash
    { 1948, 40 }, // Electro Magnifying Glass
    { 1952, 26 }, // Owlbeard
    { 1956, 27 }, // Chaos Cursed Wand
    { 1960, 28 }, // Ecto Pack
    { 1974, 26 }, // Nightmare Magnifying Glass
    { 1980, 30 }, // Claw Glove
    { 1998, 23 }, // Skeletal Dragon Claw
    { 2066, 31 }, // Cosmic Unicorn Bracelet
    { 2212, 32 }, // Black Crystal Dragon
    { 2218, 33 }, // Mighty Snow Rod
    { 2220, 34 }, // Tiny Tank
    { 2224, 12 }, // Magic Reindeer Bell
    { 2266, 35 }, // Crystal Glaive
    { 2386, 36 }, // Heavenly Scythe
    { 2388, 37 }, // Heartbreaker Hammer
    { 2450, 38 }, // Diamond Dragon
    { 2476, 39 }, // Burning Eyes
    { 2512, 41 }, // Marshmallow Basket
    { 2572, 42 }, // Flame Scythe
    { 2592, 43 }, // Legendary Katana
    { 2636, 29 }, // Rapier
    { 2720, 44 }, // Electric Bow
    { 2752, 45 }, // Pineapple Launcher
    { 2754, 46 }, // Demonic Arm
    { 2756, 47 }, // The Gungnir
    { 2800, 25 }, // Penguin Leash
    { 2802, 49 }, // Poseidon's Trident
    { 2854, 26 }, // Phoenix Pacifier
    { 2866, 50 }, // Wizard's Staff
    { 2876, 51 }, // BLYoshi's Free Dirt
    { 2878, 52 }, // FC Cleats
    { 2880, 52 }, // Man U Cleats
    { 2886, 54 }, // Baseball Glove
    { 2888, 53 }, // Pigskin
    { 2890, 55 }, // Basketball
    { 2906, 53 }, // Tennis Racquet
    { 2908, 29 }, // Carrot Sword
    { 2910, 56 }, // Emerald Staff
    { 2952, 29 }, // Digger's Spade
    { 2976, 1 }, // Cybernetic Eye
    { 2986, 70 }, // Wolf Tamer's Glove
    { 3066, 57 }, // Fire Hose
    { 3070, 29 }, // Fire Ax
    { 3108, 29 }, // Chainsaw Hand
    { 3124, 58 }, // Soul Orb
    { 3160, 29 }, // Sword And Shield
    { 3162, 29 }, // Twin Swords
    { 3166, 93 }, // Pet Slime
    { 3168, 59 }, // Strawberry Slime
    { 3206, 29 }, // Lumber Axe
    { 3214, 60 }, // Axe Of Winter
    { 3238, 61 }, // Magical Carrot
    { 3274, 62 }, // T-Shirt Cannon
    { 3300, 64 }, // Party Blaster
    { 3418, 65 }, // Serpent Staff
    { 3430, 6 }, // Lucky Pendant
    { 3466, 29 }, // Sushi Knife
    { 3476, 66 }, // Spring Bouquet
    { 3578, 19 }, // Hand Torch
    { 3588, 29 }, // Climbing Picks
    { 3596, 67 }, // Pinata Pal
    { 3678, 31 }, // Thinking Cap
    { 3680, 77 }, // Phlogiston
    { 3686, 68 }, // Toy Lock-Bot
    { 3716, 69 }, // Neutron Gun
    { 3736, 41 }, // Demon Control Cap
    { 3932, 29 }, // Rock Hammer
    { 3934, 29 }, // Rock Chisel
    { 4110, 70 }, // Flowersaurus Rex
    { 4126, 53 }, // Stone Spear
    { 4136, 4 }, // Heatbow
    { 4150, 31 }, // Eye Of Growganoth
    { 4166, 29 }, // Death's Scythe
    { 4170, 53 }, // Logarithmic Wheel
    { 4208, 39 }, // Demonic Horns
    { 4252, 70 }, // Mini-Peng
    { 4278, 53 }, // Yo-Yo
    { 4290, 71 }, // Solsascarf
    { 4294, 40 }, // Betty Bluetooth Doll
    { 4332, 7 }, // Living Dead Remote
    { 4464, 73 }, // AK-8087
    { 4474, 72 }, // Skull Launcher
    { 4506, 29 }, // Butcher Knife
    { 4508, 12 }, // Unearthly Synthoid
    { 4510, 12 }, // Radioactive Synthoid
    { 4512, 12 }, // Mysterious Synthoid
    { 4514, 12 }, // Haunted Synthoid
    { 4644, 18 }, // Saxamaphone
    { 4660, 74 }, // Confetti Cannon
    { 4664, 6 }, // Hotpants
    { 4688, 29 }, // Xenoid Claws
    { 4746, 75 }, // Diamond Horn
    { 4748, 40 }, // Diamond Horns
    { 4750, 75 }, // Diamond Devil Horns
    { 4778, 76 }, // Adventurer's Whip
    { 4840, 78 }, // Balloon Launcher
    { 4956, 29 }, // Emerald Pickaxe
    { 4996, 77 }, // Burning Hands
    { 5002, 6 }, // Playful Fire Sprite
    { 5004, 32 }, // Playful Wind Sprite
    { 5006, 32 }, // Playful Wood Sprite
    { 5008, 32 }, // Playful Water Sprite
    { 5088, 58 }, // Ghastly Robe
    { 5206, 79 }, // Cloak of Falling Waters
    { 5276, 131 }, // Celestial Lance
    { 5424, 4 }, // Winter Frost Bow
    { 5456, 4 }, // Silverstar Bow
    { 5480, 80 }, // Rayman's Fist
    { 6026, 76 }, // Whip of Truth
    { 6044, 18 }, // Fiesta Mariachi Guitar
    { 6058, 9 }, // Dual Six-Shooters
    { 6086, 69 }, // Neutron Power Glove
    { 6110, 81 }, // Pineapple Spear
    { 6298, 84 }, // Smoog the Great Dragon
    { 6308, 82 }, // Beach Ball
    { 6310, 83 }, // Watermelon Slice
    { 6312, 29 }, // Phoenix Sword
    { 6338, 41 }, // Mighty Tiger
    { 6358, 106 }, // Startopian Empire - Combat Rifle
    { 6368, 8 }, // Greezak Horde - Shoulder Cannon
    { 6382, 8 }, // Startopian Empire - Force Shield & Phase Blaster
    { 6670, 41 }, // World Cup Cleats
    { 6754, 97 }, // Squirrel King
    { 6756, 85 }, // Scepter of the Honor Guard
    { 6804, 106 }, // Bubble Gun
    { 6892, 87 }, // Sorcerer's Tunic of Mystery
    { 6966, 88 }, // Riding Bull
    { 7044, 86 }, // Jade Crescent Axe
    { 7088, 89 }, // Ezio's Armguards
    { 7098, 90 }, // Apocalypse Scythe
    { 7106, 30 }, // Cat Eyes
    { 7136, 92 }, // Ethereal Rainbow Dragon
    { 7142, 93 }, // Tamed Looming Triffid
    { 7146, 41 }, // Mountain Dire Wolf
    { 7192, 91 }, // Shadow Spirit of the Underworld
    { 7196, 95 }, // Monarch Butterfly Wings
    { 7216, 94 }, // Mad Hatter
    { 7384, 98 }, // Go-Go-Growformer!
    { 7392, 96 }, // Mage's Orb
    { 7396, 100 }, // Snowflake Eyes
    { 7402, 100 }, // Snowflake Eyes with Rugged Winter Beard
    { 7406, 177 }, // Super Snowball Launcher
    { 7408, 61 }, // Abominable Snowman Suit
    { 7414, 99 }, // Chipmunk
    { 7424, 101 }, // Snowfrost's Candy Cane Blade
    { 7434, 29 }, // Poseidon's Digger's Trident
    { 7470, 102 }, // Narwhal Tusk Staff
    { 7488, 103 }, // SLaminator's Boomerang
    { 7500, 29 }, // Nutcracker Saber
    { 7504, 17 }, // Super Squirt Rifle 500
    { 7568, 107 }, // Hovernator Drone - White
    { 7570, 107 }, // Hovernator Drone - Black
    { 7572, 107 }, // Hovernator Drone - Red
    { 7574, 107 }, // Hovernator Drone - Golden
    { 7584, 110 }, // Sprite's Robe
    { 7586, 104 }, // Sonic Buster Sword
    { 7646, 104 }, // Tyr's Spear
    { 7650, 105 }, // Mjolnir
    { 7660, 109 }, // Super Party Launcher
    { 7668, 108 }, // Air Horn
    { 7736, 111 }, // Dragon Knight's Spear
    { 7748, 12 }, // Super-Dragon Mask
    { 7750, 6 }, // Ghost Dragon Charm
    { 7754, 23 }, // Emergency Dragon Claw
    { 7758, 6 }, // Prehistoric Dragon Claw
    { 7760, 6 }, // Star Dragon Claw
    { 7826, 111 }, // Heartstaff
    { 7828, 111 }, // Golden Heartstaff
    { 7830, 111 }, // Heartsword
    { 7832, 111 }, // Golden Heartsword
    { 7836, 112 }, // Morty the Gray Elephant
    { 7838, 112 }, // Morty the Orange Elephant
    { 7840, 112 }, // Morty the Pink Elephant
    { 7842, 112 }, // Morty the Diamond Elephant
    { 7890, 29 }, // Garden Shears
    { 7912, 111 }, // War Hammers of Darkness
    { 7940, 111 }, // Flaming Boxing Gloves
    { 7950, 113 }, // Ionic Pulse Cannon Tank
    { 8002, 114 }, // Money Gun
    { 8006, 19 }, // Hellfire Horns - Black
    { 8008, 19 }, // Hellfire Horns - Blue
    { 8010, 19 }, // Hellfire Horns - Orange
    { 8012, 19 }, // Hellfire Horns - Ruby
    { 8022, 116 }, // Junk Cannon
    { 8036, 118 }, // Balloon Bunny
    { 8038, 120 }, // Pet Egg
    { 8042, 30 }, // Claws of Barko
    { 8046, 6 }, // Drabbit's Foot
    { 8354, 1 }, // Eyes on Stalks
    { 8358, 121 }, // The Beast with a Thousand Eyes Mask
    { 8360, 120 }, // Puggly
    { 8364, 122 }, // Horrid Snotty Face
    { 8372, 119 }, // Giant Eye Head
    { 8374, 120 }, // Bob The Gross Blob
    { 8432, 132 }, // Galactic Destructor - Green
    { 8434, 132 }, // Galactic Destructor - Purple
    { 8436, 132 }, // Galactic Destructor - Red
    { 8438, 123 }, // Galactic Destructor - Blue
    { 8452, 29 }, // Hands of the Void
    { 8492, 111 }, // Wormtouch
    { 8494, 126 }, // Snakewolf
    { 8510, 120 }, // Grumpy Pet Pineapple
    { 8554, 29 }, // Caduceaxe
    { 8584, 29 }, // Executioner's Axe
    { 8604, 29 }, // Crab Claw & Shield
    { 8732, 29 }, // Bamboo Sword
    { 8736, 29 }, // Obsidian Axe
    { 8810, 119 }, // Cute Bulb Mask
    { 8814, 127 }, // Slithering Serpent Hair
    { 8816, 128 }, // Astro Shades - Red
    { 8818, 128 }, // Astro Shades - Orange
    { 8820, 128 }, // Astro Shades - Purple
    { 8822, 128 }, // Astro Shades - Green
    { 8910, 129 }, // Leaf Blower
    { 8912, 32 }, // Scarecrow Pet
    { 8942, 130 }, // Dual Crescent Blade
    { 8944, 131 }, // Sun Blade
    { 8946, 133 }, // Storm Breaker
    { 8950, 132 }, // Sniper Rifle
    { 8960, 134 }, // Spring-Loaded Fists
    { 9006, 135 }, // Spirit of Anubis
    { 9032, 90 }, // Scythe of the Underworld
    { 9058, 136 }, // Cursed Katana
    { 9060, 109 }, // Hilarious Honker
    { 9062, 6 }, // Chimera's Call
    { 9066, 138 }, // Claw Of Growganoth
    { 9076, 29 }, // Superb Chef's Super Spatula
    { 9082, 137 }, // Rocket-Powered Warhammer
    { 9086, 6 }, // Friendship Dragon Claw
    { 9116, 111 }, // Red Laser Scimitar
    { 9118, 111 }, // Green Laser Scimitar
    { 9120, 111 }, // Blue Laser Scimitar
    { 9122, 111 }, // Purple Laser Scimitar
    { 9136, 139 }, // Dueling Star Fighter - Rebel Raider
    { 9138, 140 }, // Dueling Star Fighter - Imperial Enforcer
    { 9172, 141 }, // Armored WinterBot - Back
    { 9176, 141 }, // Armored WinterBot - Fists
    { 9190, 142 }, // Cloak of Fire and Ice
    { 9194, 60 }, // Hammer Of Winter
    { 9236, 145 }, // Ancient Shards
    { 9254, 143 }, // Euphoric Dragon
    { 9256, 144 }, // Party Bubble Blaster
    { 9272, 6 }, // Draconic Claw
    { 9280, 26 }, // Jin Chan Leash
    { 9304, 137 }, // Brutal Hand Fans
    { 9340, 95 }, // Datemaster's Rose
    { 9342, 146 }, // Datemaster's Bling
    { 9348, 119 }, // Medusa's Crown
    { 9360, 126 }, // Armored Riding Troll
    { 9376, 149 }, // Mining Mech
    { 9378, 148 }, // Lightning Gauntlets
    { 9396, 43 }, // Balrog's Tail
    { 9410, 150 }, // Radiant Doom Staff
    { 9450, 177 }, // Hot Cross Bun Launcher
    { 9462, 151 }, // Boastful Brawler Hair
    { 9606, 152 }, // Doomsday Warhammer
    { 9654, 12 }, // Pineapple Superhero
    { 9698, 8 }, // Arm Blaster
    { 9714, 31 }, // My Pet Sputnik
    { 9716, 153 }, // Crystal Infused Sword
    { 9732, 41 }, // Greg, The Octopus
    { 9738, 92 }, // Staff of the Deep
    { 9758, 152 }, // Neptune's Trident
    { 10046, 169 }, // Growboy
    { 10048, 167 }, // Tactical Stealth Espionage Device
    { 10050, 170 }, // Really Dangerous Pet Llama
    { 10052, 4 }, // Eldritch Crossbow
    { 10064, 168 }, // Capuchin Leash
    { 10066, 126 }, // Riding Gorilla
    { 10128, 171 }, // Mechanical Butler
    { 10130, 4 }, // Sun Shooter Bow
    { 10210, 172 }, // Cloak of Equilibrium
    { 10232, 41 }, // Reanimated Howly Hat
    { 10246, 175 }, // Celestial Dragon Charm
    { 10250, 173 }, // Ultra Sportsball Bat
    { 10278, 176 }, // Torch of Enlightenment
    { 10288, 57 }, // JackBowe's Koala
    { 10290, 80 }, // Growch Bod
    { 10292, 177 }, // Turkey Sucker
    { 10330, 178 }, // Shadow Crown
    { 10334, 111 }, // Black Balrog's Tail
    { 10336, 39 }, // Black Burning Eyes
    { 10388, 180 }, // Swordfish Sword
    { 10392, 179 }, // Water Gun Hat
    { 10402, 29 }, // Christmas Tree Sword
    { 10406, 41 }, // Staff of Deepest Winter
    { 10426, 183 }, // Mystic Snow Fox
    { 10442, 184 }, // Winter Light Launcher
    { 10494, 186 }, // Walktopian
    { 10498, 111 }, // Candy Cane Scythe
    { 10506, 185 }, // Spruce Goose
    { 10578, 111 }, // Mystic Battle Lance
    { 10604, 168 }, // 2016 - Zodiac Year of the Monkey
    { 10618, 187 }, // Perilous Pirate Ship
    { 10626, 111 }, // Rose Rifle
    { 10652, 188 }, // Love Charged Hands
    { 10670, 111 }, // Bow of the Rainbow
    { 10674, 192 }, // Crown of Seasons
    { 10676, 191 }, // Shamrock Shuriken
    { 10680, 111 }, // Finias' Red Javelin
    { 10686, 31 }, // Shady Agent Shades
    { 10694, 193 }, // Mini Minokawa
    { 10714, 194 }, // Steampunk Arm
    { 10722, 196 }, // Bunny Ear Magnifying Glass
    { 10724, 195 }, // Lil Growpeep's Baaaa Blaster
    { 10754, 197 }, // Helping Hand
    { 10780, 29 }, // Giving Growaxe
    { 10800, 198 }, // Cactus Cannon
    { 10886, 200 }, // Pineapple Chakram
    { 10888, 199 }, // Giant Pineapple Pizza Paddle
    { 10890, 202 }, // Rocket Powered Pineapple Vacuum
    { 10894, 201 }, // Pineapple Shoulder Cannon
    { 10914, 204 }, // Pure Being of Light and Shadow
    { 10922, 203 }, // Pegasus Lance
    { 10952, 207 }, // Space Badger
    { 10954, 215 }, // Space Mouse
    { 10956, 213 }, // Space Dog
    { 10958, 214 }, // Space Pig
    { 10960, 212 }, // Space Rabbit
    { 10990, 205 }, // Alien Recon Wagon
    { 10994, 41 }, // Unknown Space Pet
    { 10996, 12 }, // Space Rover
    { 10998, 206 }, // Suspicious Weather Balloon
    { 11000, 208 }, // Channel G News Van
    { 11006, 209 }, // Soul Scythe
    { 11020, 89 }, // Dark Assassin's Armguards
    { 11046, 210 }, // Hydro Cannon
    { 11052, 211 }, // Super Duper Ice Cream Scooper
    { 11066, 29 }, // Lobster Onesie Claws
    { 11076, 216 }, // Ambu-Lance
    { 11078, 31 }, // Cute Mutant Wonky
    { 11080, 31 }, // Cute Mutant Plonky
    { 11082, 31 }, // Cute Mutant Boogle
    { 11084, 217 }, // Beowulf's Blade
    { 11116, 220 }, // Crystal Crown
    { 11118, 218 }, // Sonic Buster Katana
    { 11120, 219 }, // Primordial Jade Lance
    { 11140, 225 }, // Legendary Destroyer
    { 11142, 223 }, // Legendary Owl
    { 11158, 221 }, // Zodiac Ring
    { 11162, 222 }, // Finger Gun
    { 11232, 224 }, // Ouroboros Charm
    { 11240, 227 }, // Seed Gatling Gun
    { 11248, 226 }, // Staff of S'mores
    { 11250, 228 }, // Harvest Jade Hat
    { 11284, 229 }, // Paper Wasp Pet
    { 11292, 231 }, // Lightning Umbrella
    { 11298, 111 }, // Sakura's Revenge
    { 11308, 200 }, // Royal Clam Cruiser
    { 11310, 126 }, // Trick or Treat Troll
    { 11312, 111 }, // Spider Sniper
    { 11314, 233 }, // Cauldron Cannon
    { 11316, 234 }, // Skull of Burning Horrors
    { 11324, 235 }, // Pharaoh's Pendant
    { 11326, 111 }, // Ferryman of the Underworld
    { 11354, 236 }, // Turkey Float
    { 11358, 111 }, // Super Slapshot Stick
    { 11380, 111 }, // Black Bow of the Rainbow
    { 11384, 239 }, // Matrix Visor
    { 11438, 237 }, // Digital Bow
    { 11440, 111 }, // Mystic Bow
    { 11442, 111 }, // Royal Mystic Bow
    { 11458, 240 }, // Santa's Super Sack
    { 11460, 237 }, // Snow Cone Pet
    { 11464, 237 }, // Ice Dragon Scythe
    { 11506, 245 }, // Mask of The Dragon
    { 11508, 245 }, // Royal Mask of The Dragon
    { 11536, 245 }, // Beat Box Mask
    { 11542, 237 }, // Neon Party Yo-Yo
    { 11548, 242 }, // Guardian Armor
    { 11552, 242 }, // Royal Guardian Armor
    { 11562, 245 }, // Pet Blood Dragon
    { 11620, 181 }, // 2022 - Zodiac Year of the Tiger
    { 11622, 237 }, // Draconic Soul Cannon
    { 11630, 237 }, // Monkey Warrior's Staff
    { 11674, 237 }, // Fancy Flute
    { 11704, 243 }, // Alpha Wolf
    { 11706, 243 }, // Royal Alpha Wolf
    { 11716, 237 }, // Mallet of Sucelles
    { 11718, 237 }, // Harp of Aibell
    { 11720, 245 }, // Eye of Balor
    { 11760, 237 }, // Growing Guardian Armor
    { 11762, 237 }, // Growing Guardian Gauntlets
    { 11764, 12 }, // Edison, The Lightbulb
    { 11768, 245 }, // Fish Tank Head
    { 11786, 237 }, // Soft-boiled Scepter
    { 11788, 92 }, // Easter Creature
    { 11814, 241 }, // Rabbit Top Hat
    { 11818, 248 }, // Equinox Scarf
    { 11872, 237 }, // Comida Crusher
    { 11876, 248 }, // Serpent Shoulders
    { 11882, 245 }, // Neurovision
    { 11884, 245 }, // Royal Neurovision
    { 11990, 120 }, // Baby Guzzler
    { 11994, 237 }, // Pineapple of Locksley
    { 12000, 248 }, // Pineapple Warrior Charm
    { 12014, 237 }, // Dal's Phasers
    { 12016, 237 }, // Gwyn's Heirloom
    { 12018, 237 }, // Rok's Fists
    { 12020, 237 }, // Jankom Pog's Multi-Mitt
    { 12022, 237 }, // Mek'leth
    { 12024, 237 }, // Mine Laser Drill
    { 12102, 29 }, // Zero's Arms
    { 12172, 237 }, // Ethereal Chains
    { 12176, 237 }, // Poppin' Popsicle Pickaxe
    { 12180, 244 }, // Great Ball of Fire
    { 12184, 237 }, // Pearl Bracelet
    { 12230, 237 }, // Ghost Pirate Scimitar
    { 12232, 241 }, // Neon Manta Ray
    { 12240, 248 }, // Oceanaura
    { 12242, 237 }, // bllade's Celestius
    { 12246, 237 }, // Staff of Gale
    { 12248, 237 }, // Royal Staff of Gale
    { 12286, 237 }, // Pet Pink Crocodile
    { 12300, 237 }, // Arm Blades
    { 12302, 241 }, // Lightning Horns
    { 12308, 39 }, // Eldritch Eyes
    { 12342, 111 }, // Mageblade
    { 12344, 244 }, // Lightning Boots
    { 12346, 244 }, // Royal Lightning Boots
    { 12350, 237 }, // Personal Storm Cloud
    { 12356, 237 }, // Garuda Battle Axe
    { 12368, 29 }, // Strange Eddie Axe
    { 12374, 237 }, // Mighty Legend Gauntlets
    { 12384, 237 }, // Mystical Lantern
    { 12402, 237 }, // Waay Chivo Cannon
    { 12404, 237 }, // Werewolf Claws
    { 12410, 237 }, // Hands of the Mummy
    { 12412, 237 }, // Wizard's Wand
    { 12416, 237 }, // Growmlin
    { 12420, 237 }, // Skull Surged Veins
    { 12432, 246 }, // Master Magic
    { 12434, 246 }, // Royal Master Magic
    { 12564, 248 }, // Gatling Gum
    { 12628, 237 }, // Shattered Spear
    { 12640, 246 }, // Minokawa Wings
    { 12642, 248 }, // Growtronic Halo
    { 12644, 248 }, // Royal Growtronic Halo
    { 12656, 7 }, // Pontianak Pet
    { 12658, 237 }, // Fist of Vengeance
    { 12842, 246 }, // Sgt. Stuffins' Wings
    { 12846, 237 }, // Star Spangled Shield
    { 12850, 237 }, // Growtronic Food Replicator
    { 12860, 237 }, // Black Neon Party Yo-Yo
    { 12862, 237 }, // Scepter of Shadows
    { 12870, 237 }, // Candy Cane Bow
    { 12872, 241 }, // Red Panda Spirit
    { 12874, 241 }, // Royal Red Panda Spirit
    { 12880, 237 }, // Freezing Fists
    { 12886, 237 }, // Icicle Lance
    { 12958, 241 }, // Infinity Crown
    { 12990, 237 }, // Disco Fever
    { 12992, 237 }, // Lightning Guitar
    { 13022, 248 }, // Scarf of Chains
    { 13024, 248 }, // Royal Scarf of Chains
    { 13058, 244 }, // Riding Nian
    { 13060, 237 }, // Lunar Drum
    { 13114, 237 }, // Stained Glass Scepter
    { 13116, 245 }, // Crimson Thief Mask
    { 13118, 237 }, // Bear That Cares
    { 13136, 237 }, // Golden Stained Glass Scepter
    { 13188, 237 }, // Microphone Madness
    { 13190, 237 }, // Dagger of Time
    { 13268, 246 }, // Jellyfish Trio
    { 13322, 244 }, // The Oillipheist
    { 13324, 241 }, // Werewolf Hood of Ossory
    { 13326, 237 }, // Shamrock Staff
    { 13330, 237 }, // Midas Touch
    { 13332, 237 }, // Corned Beef Drum Shotgun
    { 13366, 237 }, // Carrot Lance
    { 13396, 248 }, // World Owner Blingin' Chain
    { 13398, 248 }, // Royal World Owner Blingin' Chain
    { 13410, 237 }, // Healing Beam
    { 13424, 241 }, // Aztec Crown
    { 13484, 237 }, // Black Tie Baller Cane
    { 13486, 237 }, // Pineapple Bussin' Sword
    { 13488, 237 }, // Kiwi Bussin' Sword
    { 13490, 237 }, // Papaya Bussin' Sword
    { 13492, 237 }, // Watermelon Bussin' Sword
    { 13494, 237 }, // Dragon Fruit Bussin' Sword
    { 13498, 244 }, // Fruit Brigade
    { 13552, 237 }, // Mighty Anchor
    { 13554, 237 }, // Royal Mighty Anchor
    { 13572, 237 }, // Phoenix Sickles
    { 13578, 237 }, // Pearl Sparkled Shield
    { 13606, 237 }, // Ultraviolet Sword
    { 13710, 136 }, // Dark Magic Katana
    { 14220, 237 }, // Rocket Lance
    { 14222, 237 }, // Royal Rocket Lance
    { 14302, 237 }, // Qays & Layla
    { 14562, 46 }, // Kiwi Kiwi Pet
    { 14564, 237 }, // Kiwi Koala Ears
    { 14566, 237 }, // Majestic Pineapple Cape
    { 14568, 237 }, // Papaya Puppy Pet
    { 14570, 237 }, // Pineapple Conure
    { 14572, 237 }, // Watermelon Dress
};

u_char get_punch_id(u_int item_id)
{
    const auto it = std::ranges::lower_bound(punch_ids, item_id, {}, &std::pair<u_short, u_char>::first);
    return (it != std::ranges::end(punch_ids) && it->first == item_id) ? it->second : 0;
}

void punch(ENetEvent& event, const std::string_view text) 
{
    const std::string id{ command_arg(text) };
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    pPeer->punch_effect = (u_char)atoi(id.c_str());
    on::SetClothing(event.peer);
}