use std::time::Duration;

use clap::Parser;
use clap::error::ErrorKind;
use unreal_cli::cli::{Cli, OutputFormat, run};
use unreal_cli::core::envelope::Envelope;
use unreal_cli::core::error::CliError;

fn wants_json(args: &[String]) -> bool {
    args.windows(2)
        .any(|w| w[0] == "--output" && w[1] == "json")
        || args.iter().any(|a| a == "--output=json")
}

fn main() {
    let args = unreal_cli::core::msys::fix_args(std::env::args().collect());
    let cli = match Cli::try_parse_from(&args) {
        Ok(cli) => cli,
        Err(e)
            if matches!(e.kind(), ErrorKind::DisplayHelp | ErrorKind::DisplayVersion)
                || !wants_json(&args) =>
        {
            e.exit()
        }
        Err(e) => {
            let msg = e.render().to_string();
            let first = msg
                .lines()
                .next()
                .unwrap_or("invalid arguments")
                .trim_start_matches("error: ")
                .to_string();
            let err = CliError::usage("args.parse", first)
                .with_hint("see: unreal-cli <command> --help")
                .with_details(serde_json::json!(msg));
            let env = Envelope::from_result("parse", Err(err), Duration::ZERO);
            env.print(OutputFormat::Json);
            std::process::exit(env.exit_code());
        }
    };
    std::process::exit(run(cli));
}
