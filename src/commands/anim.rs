use clap::Subcommand;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;

#[derive(Debug, Subcommand)]
pub enum AnimCmd {
    /// IK Rig for a skeletal mesh, with auto-generated retarget chains and full-body IK
    Ikrig {
        path: String,
        #[arg(long)]
        mesh: String,
        /// Empty rig (no auto chains or solver)
        #[arg(long)]
        empty: bool,
    },
    /// IK Retargeter from one IK Rig to another, chains mapped automatically
    Retargeter {
        path: String,
        #[arg(long)]
        source: String,
        #[arg(long)]
        target: String,
    },
    /// Retarget animation assets with an IK Retargeter into new assets
    Retarget {
        retargeter: String,
        /// Animation assets (sequences, montages, blend spaces)
        #[arg(required = true)]
        assets: Vec<String>,
        /// Folder for the results (default: next to the originals)
        #[arg(long)]
        folder: Option<String>,
        #[arg(long)]
        suffix: Option<String>,
    },
    /// Control Rig Blueprint with the mesh's bones
    Controlrig {
        path: String,
        #[arg(long)]
        mesh: String,
        #[arg(long)]
        modular: bool,
    },
    /// Motion Matching: Pose Search Schema and Database with animations (PoseSearch plugin)
    Posesearch {
        path: String,
        #[arg(long)]
        skeleton: String,
        #[arg(required = true)]
        animations: Vec<String>,
    },
}

pub fn run(ctx: &mut Ctx, cmd: AnimCmd) -> Result<Value, CliError> {
    let (tool, args) = match cmd {
        AnimCmd::Ikrig { path, mesh, empty } => (
            "anim_ikrig_create",
            json!({ "path": path, "mesh": mesh, "auto": !empty }),
        ),
        AnimCmd::Retargeter {
            path,
            source,
            target,
        } => (
            "anim_retargeter_create",
            json!({ "path": path, "source": source, "target": target }),
        ),
        AnimCmd::Retarget {
            retargeter,
            assets,
            folder,
            suffix,
        } => {
            let mut a = json!({ "retargeter": retargeter, "assets": assets });
            if let Some(f) = folder {
                a["folder"] = json!(f);
            }
            if let Some(s) = suffix {
                a["suffix"] = json!(s);
            }
            ("anim_retarget", a)
        }
        AnimCmd::Controlrig {
            path,
            mesh,
            modular,
        } => (
            "controlrig_create",
            json!({ "path": path, "mesh": mesh, "modular": modular }),
        ),
        AnimCmd::Posesearch {
            path,
            skeleton,
            animations,
        } => (
            "posesearch_create",
            json!({ "path": path, "skeleton": skeleton, "animations": animations }),
        ),
    };
    ctx.call_with_timeout(tool, args, 300_000)
}
