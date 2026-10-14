use clap::Parser;
use unreal_cli::cli::{Cli, run};

fn main() {
    let cli = Cli::parse();
    let code = run(cli);
    std::process::exit(code);
}
