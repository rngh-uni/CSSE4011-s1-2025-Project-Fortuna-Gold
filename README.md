# CSSE4011-s1-2025-Project-Fortuna-Gold
CSSE4011 Project for Semester 1 2025 - Group Fortuna Gold

Node Communication Protocol:

Base Node -> Mobile Node:

  UUID = 1abbe1eddeadfa11baff1edbee5f1055 \[16 Bytes\]
  
  1st byte of major is the command (1 = sensor request, 2 = mode select)
  
  2nd byte of major is the sensor flag request (0 = none, 1 = temperature, 2 = humidity, 4 = C02, 8 = TVOC) (example: temp+C02 = 00000101)
  
  1st byte of minor is mode selection (0 = none, 1 = toggle, 2 = discrete, 4 = continuous)
  
  2nd byte of minor is reserved
  
  
Mobile Node -> Base Node:

  UUID = ca11edba1dfacade7a1e \[10 bytes\]

  2 bytes of UUID slots: 
    1st - Sensor flag (which sensor the data is coming from)
    2nd - RESERVED
  4 bytes of UUID & 4 bytes of Major and Minor:
    8 bytes - double precision floating point in network order (big endian)

Base node -> Viewer Node:

  UUID = cab1eb1adeca5cade0af \[10 bytes\]
