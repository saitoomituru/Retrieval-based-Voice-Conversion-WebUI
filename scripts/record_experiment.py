#!/usr/bin/env python3
"""RVC macOS/AU 移植の実験票を生成する小さな receipt writer。"""

from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPERIMENTS = ROOT / "experiments"
VALID_RESULTS = {"success", "failure", "blocked", "not-tested", "auto"}


def git_value(*args: str) -> str:
    try:
        proc = subprocess.run(
            ["git", *args],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        return proc.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def shell_run(command: str) -> tuple[int, str, str]:
    proc = subprocess.run(
        command,
        cwd=ROOT,
        shell=True,
        text=True,
        capture_output=True,
    )
    return proc.returncode, proc.stdout, proc.stderr


def fence(text: str) -> str:
    return text.rstrip() if text.strip() else "(出力なし)"


def main() -> int:
    parser = argparse.ArgumentParser(description="実験・開発 receipt を experiments/ へ保存します。")
    parser.add_argument("--slug", required=True, help="ファイル名に使う短い識別子")
    parser.add_argument("--issue", default="unknown", help="対象 Issue 番号")
    parser.add_argument("--result", default="not-tested", choices=sorted(VALID_RESULTS))
    parser.add_argument("--summary", default="", help="目的または短い説明")
    parser.add_argument("--run", help="repository root で実行し stdout/stderr を記録するコマンド")
    args = parser.parse_args()

    safe_slug = "".join(c if c.isalnum() or c in "-_" else "-" for c in args.slug).strip("-")
    if not safe_slug:
        parser.error("--slug に利用可能な文字がありません。")

    now = dt.datetime.now().astimezone()
    branch = git_value("branch", "--show-current")
    head = git_value("rev-parse", "HEAD")
    status = git_value("status", "--short")

    exit_code = None
    stdout = ""
    stderr = ""
    result = args.result

    if args.run:
        exit_code, stdout, stderr = shell_run(args.run)
        if result == "auto":
            result = "success" if exit_code == 0 else "failure"
    elif result == "auto":
        result = "not-tested"

    EXPERIMENTS.mkdir(parents=True, exist_ok=True)
    path = EXPERIMENTS / f"{now:%Y%m%d-%H%M}__{safe_slug}.ja.md"
    if path.exists():
        suffix = now.strftime("%S")
        path = EXPERIMENTS / f"{now:%Y%m%d-%H%M%S}__{safe_slug}.ja.md"

    lines = [
        f"# 実験・開発ログ: {safe_slug}",
        "",
        f"実施時刻: {now.isoformat(timespec='seconds')}",
        f"対象 Issue: #{args.issue}" if str(args.issue).isdigit() else f"対象 Issue: {args.issue}",
        f"branch / HEAD: {branch or 'detached'} / {head}",
        f"結果: {result}",
        "",
        "## 目的",
        "",
        args.summary or "(未記入)",
        "",
        "## 入力・前提",
        "",
        "```text",
        "git status --short:",
        fence(status),
        "```",
        "",
        "## 実行コマンド",
        "",
        "```text",
        args.run or "(未実行)",
        "```",
        "",
        "## 観測事実",
        "",
    ]

    if args.run:
        lines += [
            f"exit code: {exit_code}",
            "",
            "### stdout",
            "",
            "```text",
            fence(stdout),
            "```",
            "",
            "### stderr",
            "",
            "```text",
            fence(stderr),
            "```",
        ]
    else:
        lines.append("(未記入)")

    lines += [
        "",
        "## 解釈 / 仮説",
        "",
        "(未記入)",
        "",
        "## Recovery / 次の一手",
        "",
        "(未記入)",
        "",
        "## unknown",
        "",
        "(未記入)",
        "",
    ]

    path.write_text("\n".join(lines), encoding="utf-8")
    print(path.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
