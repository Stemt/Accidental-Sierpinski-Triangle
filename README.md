# Accidental-Sierpinski-Triangle

![./sierpinski.gif](https://github.com/Stemt/Accidental-Sierpinski-Triangle/blob/main/sierpinski.gif?raw=true)

While messing with a computer vision concept I got jumpscared by a Sierpiński triangle. This is an archive for the code that generated it.

## Requirements

- linux with X11 window manager
- ffmpeg is installed
- raylib is installed
- c compiler is available under the 'cc' alias

## To Build

This application uses the [no-build](https://github.com/tsoding/nob.h) build system.

To start, bootstrap the no-build build system.
```
cc -o nob nob.c
```

Run the build (will also run app automatically).
```
./nob
```

You should now in the window see 3 seperate instances of the recording.

## To See The The Triangle

Simply fullscreen the window in on the monitor that is being captured and the triangle should appear.

