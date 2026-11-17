# Gemini Code Review (automated PR reviews)

A small custom GitHub Action that has Gemini review pull requests and post
the review as a PR comment. It replaces the third-party action from the
pasted guide (`leonardosalas/code-review-bot@v1`), which does not exist
on GitHub (404) — this version calls the Gemini API directly instead, so
there is no third-party action to trust.

## Files

- `gemini-review.yml` — the workflow. Triggers on pull requests
  (`opened`, `synchronize`, `reopened`), one job on `ubuntu-latest`.
- `gemini-review.py` — the review script. Pure Python standard library
  (urllib only), nothing to install. It fetches the PR diff from the
  GitHub API, sends it to Gemini, and posts (or updates) a PR comment.

## Setup (Ed)

1. **Get a free Gemini API key.** Go to Google AI Studio
   (https://aistudio.google.com), sign in with a Google account, click
   "Get API key", create a new key, and copy it. The free tier covers
   personal-repo prototyping use.
2. **Add it as a repo secret.** In the GitHub repo, go to Settings >
   Secrets and variables > Actions > New repository secret. Name it
   `GEMINI_API_KEY` and paste the key as the value.
3. **Commit and push these files.** Until then the workflow does not
   exist on GitHub and nothing runs. Committing and pushing is Ed's
   call, as with any repo change.

## What it does

- On every PR open, update, or reopen, it reviews the diff with
  `gemini-2.5-flash` and posts a short comment: real bugs, crashes,
  memory errors, logic errors, security issues, and correctness
  problems only. No style or formatting nitpicks.
- If nothing is wrong it says "No issues found."
- The review is capped (2,048 output tokens, prompt asks for under
  300 words) so comments stay readable.
- Diffs over 12,000 characters are truncated to the first 12,000 and
  the comment says so.
- Empty diffs post nothing.
- On later pushes to the same PR it updates its own comment instead of
  posting a new one, so the timeline does not pile up.

## Notes and limits

- The key is read from the `GEMINI_API_KEY` secret and is only ever
  sent to Google in the `x-goog-api-key` header. It is never printed
  or written to a file.
- If the secret is missing, the job fails with a clear message saying
  the secret is not set.
- The workflow uses the `pull_request` event, so it works for PRs from
  branches in this repo. PRs from forks run with a read-only token and
  cannot post comments; switching to `pull_request_target` would fix
  that but has security implications for untrusted code, so it was
  deliberately not used.
- Model and endpoint were verified against Google's Gemini API docs
  (Nov 17, 2026): `POST
  https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent`.
  The classic `generateContent` REST API is now labelled legacy by
  Google in favour of a newer Interactions API, but it remains
  supported and is the right fit for this simple stateless job.
