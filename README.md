# autokstuff

Automatically pause Kstuff when running homebrew games on your PS5 using the Kstuff.

### Auto Enabled/Disable kstuff

   `kstuff` is automatically disabled on launching the game,\
   And then enabled after closing the game.\
   \
   To eanble the automode.\
   Create `autokstuff` file in the root folder of your dump.\
   In the file is a single line number, means the seconds to wait until disable `kstuff`.\
   \
   For PS4 fpkg game\
   You can also create a file named by title id in `/data/autokstuff/`.\
   For example `/data/autokstuff/PPSA12345`\
   \
   Thanks\
       `https://github.com/EchoStretch/kstuff-toggle`\
       `https://github.com/BestPig/BackPork`
