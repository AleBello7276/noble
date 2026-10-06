# noble

## Info

Introducing *noble* (previously was LLVM360) a little personal experimental xbox 360 emulator using Cranelift as a codegen optimiser backend.

--------------- 
Join the project [Discord][dis] server! 

### Name
*noble*, refers to Noble gasses of which Xenon is a part of them and is also the codename of the console.

## Why Rust Crates in a C++ project
Great question.. well those crates are Great so i don't have to reinvent the wheel, but why not making the Emulator in Rust? Because i only know how to make thin FFI and i don't want to make this already hard project into learning rust, so the anwser is lazyness, maybe in the far future a rust port is possible. but really, no.

## Building
- Clang compiler is required
- Cargo and a Rust compiler is required to compile Cranelift and the FFI
- Git Clone this repository
- idk that's it
- If everything is good and i or you didn't messed up something, it should compile fine

## Guest debugger

An instruction stepper is available through `noble-debug` or the optional FTXUI frontend with `--debug`

## Configuration

Settings are loaded from `noble.toml` beside the executable generated on first boot

## Contributing
if want to contribute, make a fork and a PR, also join the discord server! 

## Credits

A lot of the research, HLE implementations, some instructions, and GPU is derived directly from Xenia emulator, complete Credits to the project and all the contributors for the code, progress wont be fast without the years of research put into it.

[dis]: https://discord.gg/JufwFS9mmf
