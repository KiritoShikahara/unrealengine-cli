# Homebrew formula template; url and sha256 are filled in by the release process.
class UnrealCli < Formula
  desc "CLI that lets AI agents drive Unreal Editor"
  homepage "https://github.com/KiritoShikahara/unreal-cli"
  version "0.1.0"
  on_arm do
    url "https://github.com/KiritoShikahara/unreal-cli/releases/download/v0.1.0/unreal-cli-aarch64-apple-darwin.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end
  on_intel do
    url "https://github.com/KiritoShikahara/unreal-cli/releases/download/v0.1.0/unreal-cli-x86_64-apple-darwin.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  def install
    bin.install "unreal-cli"
  end

  test do
    assert_match "version", shell_output("#{bin}/unreal-cli --output json version")
  end
end
