# Homebrew formula template; url and sha256 are filled in by the release process.
class UnrealengineCli < Formula
  desc "CLI that lets AI agents drive Unreal Editor"
  homepage "https://github.com/KiritoShikahara/unrealengine-cli"
  version "0.2.0"
  on_arm do
    url "https://github.com/KiritoShikahara/unrealengine-cli/releases/download/v0.2.0/unrealengine-cli-aarch64-apple-darwin.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end
  on_intel do
    url "https://github.com/KiritoShikahara/unrealengine-cli/releases/download/v0.2.0/unrealengine-cli-x86_64-apple-darwin.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  def install
    bin.install "unrealengine-cli"
  end

  test do
    assert_match "version", shell_output("#{bin}/unrealengine-cli --output json version")
  end
end
