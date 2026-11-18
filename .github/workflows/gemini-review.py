#!/usr/bin/env python3
"""Gemini code review bot for Shattered Shield pull requests.

Called by .github/workflows/gemini-review.yml. Pure standard library
(urllib only), so it runs on a stock ubuntu-latest runner with nothing
to install.

What it does:
  1. Fetches the PR diff from the GitHub API (GITHUB_TOKEN).
  2. Sends the diff to the Gemini API (GEMINI_API_KEY, model from
     GEMINI_MODEL, default gemini-flash-latest) and asks for a short review
     focused on real bugs, security, and correctness - no style nitpicks.
  3. Posts the review as a PR comment. On later pushes to the same PR it
     updates its own comment instead of posting a new one.

Environment (set by the workflow):
  GITHUB_REPOSITORY  owner/repo
  PR_NUMBER          pull request number
  GITHUB_TOKEN       secrets.GITHUB_TOKEN (post/update the comment)
  GEMINI_API_KEY     secrets.GEMINI_API_KEY (free key from Google AI Studio)
  GEMINI_MODEL       optional, defaults to gemini-flash-latest

Limits:
  - Diffs over MAX_DIFF_CHARS are truncated to the first 12,000
    characters and the comment says so.
  - Empty diffs post nothing.
  - The review itself is capped at 2,048 output tokens and the prompt
    asks for under 300 words, so comments stay readable.
"""

import json
import os
import sys
import urllib.error
import urllib.request

MARKER = "<!-- gemini-review-bot -->"
MODEL = os.environ.get("GEMINI_MODEL", "gemini-flash-latest")
MAX_DIFF_CHARS = 12000
MAX_REVIEW_TOKENS = 2048
GEMINI_URL = (
    "https://generativelanguage.googleapis.com/v1beta/models/"
    f"{MODEL}:generateContent"
)


def fail(message):
    print(f"gemini-review: error: {message}", file=sys.stderr)
    sys.exit(1)


def gh_request(method, url, token, body=None):
    """One GitHub REST call. Returns the decoded JSON payload."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/vnd.github+json")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            text = resp.read().decode() or "{}"
            return json.loads(text)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode(errors="replace")
        fail(f"GitHub API {method} {url} failed ({exc.code}): {detail[:500]}")


def get_diff(repo, pr_number, token):
    """Fetch the PR diff as plain text via the GitHub API."""
    url = f"https://api.github.com/repos/{repo}/pulls/{pr_number}"
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Accept", "application/vnd.github.diff")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return resp.read().decode(errors="replace")
    except urllib.error.HTTPError as exc:
        fail(f"could not fetch PR diff ({exc.code}): "
             f"{exc.read().decode(errors='replace')[:500]}")


def gemini_review(api_key, diff):
    """Send the diff to Gemini and return the review text."""
    prompt = (
        "You are reviewing a pull request diff for Shattered Shield, a C++ game "
        "built on the Cube 2 engine. Report only real problems: bugs, crashes, "
        "memory errors, logic errors, security issues, and correctness problems. "
        "Do not comment on style, formatting, naming, or minor preferences. "
        "If nothing is wrong, say \"No issues found.\" Keep the whole review under "
        "300 words. Use short bullet points, one per issue, with file and line when "
        "you can tell. Do not repeat the diff.\n\n"
        "```diff\n" + diff + "\n```"
    )
    body = {
        "contents": [{"parts": [{"text": prompt}]}],
        "generationConfig": {
            "maxOutputTokens": MAX_REVIEW_TOKENS,
            "temperature": 0.2,
        },
    }
    req = urllib.request.Request(
        GEMINI_URL, data=json.dumps(body).encode(), method="POST"
    )
    req.add_header("Content-Type", "application/json")
    req.add_header("x-goog-api-key", api_key)
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            payload = json.loads(resp.read().decode())
    except urllib.error.HTTPError as exc:
        fail(f"Gemini API failed ({exc.code}): "
             f"{exc.read().decode(errors='replace')[:500]}")
    try:
        parts = payload["candidates"][0]["content"]["parts"]
        return "".join(part.get("text", "") for part in parts).strip()
    except (KeyError, IndexError, TypeError):
        fail(f"unexpected Gemini response: {json.dumps(payload)[:500]}")


def main():
    repo = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("PR_NUMBER")
    token = os.environ.get("GITHUB_TOKEN")
    api_key = os.environ.get("GEMINI_API_KEY")
    if not repo or not pr_number:
        fail("GITHUB_REPOSITORY and PR_NUMBER must be set (the workflow sets them).")
    if not token:
        fail("GITHUB_TOKEN is not set.")
    if not api_key:
        fail("GEMINI_API_KEY secret is not set. Add it under Settings > "
             "Secrets and variables > Actions.")

    diff = get_diff(repo, pr_number, token)
    if not diff.strip():
        print("gemini-review: PR diff is empty, nothing to review.")
        return 0

    truncated = len(diff) > MAX_DIFF_CHARS
    if truncated:
        diff = diff[:MAX_DIFF_CHARS]

    review = gemini_review(api_key, diff) or "No issues found."

    note = ""
    if truncated:
        note = (f"\n\n> Note: the diff was truncated to the first "
                f"{MAX_DIFF_CHARS} characters for review.")
    body = (
        f"{MARKER}\n"
        f"## Gemini code review\n\n"
        f"{review}"
        f"{note}\n\n"
        f"---\n"
        f"*Automated review of PR #{pr_number} by {MODEL}. Only real bugs, "
        f"security, and correctness issues are flagged; style is ignored.*"
    )

    # Update our own earlier comment if there is one, so repeated pushes
    # to the same PR do not pile up a new comment each time.
    comments = gh_request(
        "GET",
        f"https://api.github.com/repos/{repo}/issues/{pr_number}/comments"
        "?per_page=100",
        token,
    )
    existing = None
    for comment in comments:
        if isinstance(comment, dict) and comment.get("body", "").startswith(MARKER):
            existing = comment
            break

    if existing:
        gh_request(
            "PATCH",
            f"https://api.github.com/repos/{repo}/issues/comments/{existing['id']}",
            token,
            {"body": body},
        )
        print(f"gemini-review: updated existing review comment on PR #{pr_number}.")
    else:
        gh_request(
            "POST",
            f"https://api.github.com/repos/{repo}/issues/{pr_number}/comments",
            token,
            {"body": body},
        )
        print(f"gemini-review: posted review comment on PR #{pr_number}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
