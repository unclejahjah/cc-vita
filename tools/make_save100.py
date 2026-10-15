# make_save100.py OUT: a "CCS2" save (cc_host.c's format) for testing: the Blue Knight at
# level 99 with everything unlocked, laid out the way the game's own f_LoadCharacterData /
# f_SaveCharacterData read and write the storage (player 1's port).
import struct, sys

S = {}                          # address -> value, port 0

def put(addr, v): S[addr] = v

# global block: arenas (7), animals (8..11), items (12..27), expansion items (28..35)
put(7, 0xFF)
for a in range(8, 36): put(a, 0xFF)

CHAR_OFFSET, CHAR_SIZE = 64, 48
for t in range(1, 32):          # every character unlocked (bit 128 of the first byte)
    put(CHAR_OFFSET + CHAR_SIZE * (t - 1), 128)

c = CHAR_OFFSET + CHAR_SIZE * (3 - 1)   # p_type 3: the Blue Knight
fields = [128,                  # unlocked
          98,                   # level - 1 (level 99)
          0, 0, 0, 0,           # exp: 0 -> the game fills in level 99's
          39,                   # weapon: the Blue Knight's own
          0,                    # animal
          25, 25, 25, 25,       # strength, defense, magic, agility: 100 points at 99
          0xFF, 0xFF, 0xFF,     # normal-mode levels completed
          9, 9, 9,              # potions, bombs, sandwiches (the game caps them at 9)
          0xFF]                 # all 8 relics
gold = 9999999
fields += [(gold >> 24) & 255, (gold >> 16) & 255, (gold >> 8) & 255, gold & 255]
fields += [0,                   # insane mode off (start in normal mode)
           0xFF, 0xFF, 0xFF]    # insane-mode levels completed
for i, v in enumerate(fields): put(c + i, v)

with open(sys.argv[1], 'wb') as f:
    f.write(b'CCS2')
    items = sorted((a, v) for a, v in S.items() if v)
    f.write(struct.pack('<I', len(items)))
    for a, v in items:
        f.write(struct.pack('<I', a))       # port 0 << 16 | address
        f.write(struct.pack('<d', float(v)))
print(len(items), 'values')
