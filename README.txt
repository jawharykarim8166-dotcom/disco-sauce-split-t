DISCO SAUCE - SPLIT'T   (flowmotionlb)
======================================

HOW TO BUILD (same as Disco Sauce 1)
 1. Create a new GitHub repository and upload everything in this folder (keep the folder structure,
    including the hidden ".github" folder).
 2. Open the repo -> "Actions" tab -> "Build Disco Sauce Splitt" -> wait for the green tick
    (or press "Run workflow").
 3. Open the finished run -> download the artifact "DiscoSauceSplitt-Windows".

HOW TO INSTALL IN FL STUDIO
 1. Copy the whole folder "Disco Sauce Splitt.vst3" (from the VST3 folder of the download) into
        C:\Program Files\Common Files\VST3
 2. FL Studio -> Options -> Manage plugins -> "Find installed plugins".
 3. Add "Disco Sauce Splitt" on a mixer insert (or as an effect on the track you want to split).

HOW TO USE
 A) PLAY mode: put the plugin on the insert that carries your song, press PLAY in FL Studio.
    The plugin listens while the song plays (up to 5 minutes, it restarts every time you press play).
 B) FILE mode: drag any audio file (wav / mp3 / flac / aiff / ogg) onto the plugin window.
 Then press the big SPLIT button.  The screen shows BPM, KEY and SCALE, and the 3 stems light up:
 MELODY, DRUMS & PERC, BASS.  Drag a stem from the plugin straight into the FL Studio playlist.
 Double-click a stem to open its folder.  Files are saved in
        Documents\Disco Sauce Splitt Stems\<track name + time>\
 Click the BPM number to flip between x2 and /2 (half-time / double-time).
 The LEVEL slider sets the volume of the exported stems (-24 dB to +6 dB).
