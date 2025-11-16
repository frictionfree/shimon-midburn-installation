# Audio Files for Shimon Game

This directory contains the MP3 audio files required for the Shimon Butterfly Simon Says game.

## Directory Structure

Copy these files to your MicroSD card for the DFPlayer Mini module:

### mp3 Directory (`/mp3/`) - Main Game Sounds
All 39 audio files in single directory structure:

**Core Game Audio (0001-0015.mp3)**
- `mp3/0001.mp3` - Invite: "Come play with the butterfly!"
- `mp3/0002.mp3` - Invite: "Test your memory skills!"
- `mp3/0003.mp3` - Invite: "Ready for a challenge?"
- `mp3/0004.mp3` - Invite: "The butterfly wants to play!"
- `mp3/0005.mp3` - Invite: "Can you follow the pattern?"
- `mp3/0007.mp3` - Legacy My Turn: "My turn - watch carefully!" (fallback only)
- `mp3/0008.mp3` - Legacy Your Turn: "Your turn - repeat the sequence!" (fallback only)
- `mp3/0011.mp3` - Instructions (General): "Watch the colors, then repeat the sequence"
- `mp3/0012.mp3` - Instructions (Blue): "Blue butterfly instructions"
- `mp3/0013.mp3` - Instructions (Red): "Red butterfly instructions"
- `mp3/0014.mp3` - Instructions (Green): "Green butterfly instructions"
- `mp3/0015.mp3` - Instructions (Yellow): "Yellow butterfly instructions"

**"My Turn" Variations (0021-0025.mp3)**
- `mp3/0021.mp3` - My Turn Variation 1: "My turn!"
- `mp3/0022.mp3` - My Turn Variation 2: "Watch carefully!"
- `mp3/0023.mp3` - My Turn Variation 3: "Here's the pattern!"
- `mp3/0024.mp3` - My Turn Variation 4: "Follow this sequence!"
- `mp3/0025.mp3` - My Turn Variation 5: "Pay attention!"

**"Your Turn" Variations (0031-0035.mp3)**
- `mp3/0031.mp3` - Your Turn Variation 1: "Your turn!"
- `mp3/0032.mp3` - Your Turn Variation 2: "Now you try!"
- `mp3/0033.mp3` - Your Turn Variation 3: "Repeat the pattern!"
- `mp3/0034.mp3` - Your Turn Variation 4: "Show me the sequence!"
- `mp3/0035.mp3` - Your Turn Variation 5: "Your move!"

**Positive Feedback Variations (0041-0045.mp3)**
- `mp3/0041.mp3` - Positive Feedback Variation 1: "Great job!"
- `mp3/0042.mp3` - Positive Feedback Variation 2: "Perfect!"
- `mp3/0043.mp3` - Positive Feedback Variation 3: "Excellent!"
- `mp3/0044.mp3` - Positive Feedback Variation 4: "Well done!"
- `mp3/0045.mp3` - Positive Feedback Variation 5: "Correct!"

**Error & End Game Audio (0051-0053.mp3)**
- `mp3/0051.mp3` - Wrong: "Oops! That's not right."
- `mp3/0052.mp3` - Game Over: "Game over! Thanks for playing!"
- `mp3/0053.mp3` - Timeout: "Time's up! Game over."

### Color Audio Files (0061-0064.mp3)
Color names for confuser mode:
- `mp3/0061.mp3` - "Red"
- `mp3/0062.mp3` - "Blue" 
- `mp3/0063.mp3` - "Green"
- `mp3/0064.mp3` - "Yellow"

### Score Audio Files (0070-0170.mp3)
Score announcements (base 70 + score value):
- `mp3/0070.mp3` - "Zero points"
- `mp3/0071.mp3` - "One point"
- `mp3/0072.mp3` - "Two points"
- `mp3/0075.mp3` - "Five points"
- `mp3/0080.mp3` - "Ten points"
- `mp3/0095.mp3` - "Twenty-five points"
- `mp3/0120.mp3` - "Fifty points"
- `mp3/0170.mp3` - "One hundred points! Excellent!"

## Multiple Audio Variations Feature

The game now includes **anti-repetition audio variations** to eliminate monotony:

- **"My Turn" Messages**: 5 different announcements (0021-0025.mp3) randomly selected
- **"Your Turn" Messages**: 5 different prompts (0031-0035.mp3) randomly selected  
- **Positive Feedback**: 5 different celebrations (0041-0045.mp3) randomly selected
- **Anti-Repetition Logic**: Avoids playing the same variation twice in a row

### Debug Output Example
```
[AUDIO] Selected My Turn variation 3 (file 0023.mp3), avoiding last: 1
[AUDIO] My Turn variation from /mp3/0023.mp3 (DFPlayer.playMp3Folder(23))
```

## Audio Specifications
- **Format**: MP3, 16-bit, mono recommended
- **Sample Rate**: 22kHz or 44.1kHz
- **Bitrate**: 128kbps recommended
- **Volume**: Normalize to consistent levels
- **Length**: Keep concise (1-3 seconds for most sounds)
- **Voice**: Clear, friendly tone suitable for all ages

## Migration from Folder Structure

This version **eliminates the old folder structure** (`/01/`, `/02/`) and uses a **single `/mp3/` directory** for all audio files:

- **Before**: `/01/001.mp3` → **Now**: `/mp3/0062.mp3` (Blue)
- **Before**: `/02/005.mp3` → **Now**: `/mp3/0075.mp3` (5 points)
- **Benefit**: Eliminates DFPlayer addressing conflicts and simplifies file management

## Usage Notes
1. Copy all files to your MicroSD card maintaining the exact `/mp3/` directory structure
2. Insert SD card into DFPlayer Mini module
3. Build with `pio run -e hardware` for real hardware with audio
4. For simulation testing, use `pio run -e sim` (audio will print to Serial)
5. Monitor Serial output to see which audio variations are selected during gameplay