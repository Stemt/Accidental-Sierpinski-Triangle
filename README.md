# Accidental-Sierpi-ski-Triangle
While messing with a computer vision concept I got jumpscared by a Sierpiński triangle. This is an archive for the code that generated it.

## Requirements

- linux with X11 window manager
- ffmpeg is installed
- raylib is installed
- c compiler is available under the 'cc' alias

## To Build

Bootstrap the no-build build system.
```
cc -o nob nob.c
```

Run the build (will also run app automatically).
```
./nob
```

You should now in the window see 3 seperate instances of the recording.

## To See The The Triangle

Simply fullscreen the window in the window that is being captured and the triangle should appear.
