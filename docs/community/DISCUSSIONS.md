# Community Discussions setup

The former repository was deleted. These files do not restore hosted
Discussions, categories, posts or comments. The owner must configure them
on the future repository; no historical posts or comments have been recovered
here. Use Issues for reproducible defects, Discussions for collaboration and
Pull Requests for changes.

## Approved category configuration — prepared, not applied

The former repository was deleted. No categories, posts, comments, reactions
or answers were recovered. This exact configuration is prepared for the
future `Janus5G/RROS` repository; it is not live configuration.

Enable Issues and Discussions after the owner creates and authorizes the new
repository:

```sh
gh repo edit Janus5G/RROS --enable-issues --enable-discussions
```

GitHub documents category creation and editing through the authenticated
**Discussions → Categories → pencil** interface. The available GitHub CLI
repository-edit command enables the features but does not manage categories.
The repository GraphQL schema exposes category reads but no category
create/update mutation. Do not call invented endpoints. Once authorized,
reuse matching defaults and create only missing categories; do not delete
existing discussions. GitHub's documented UI is therefore required for
category details.

| Category | Icon | Format | Exact description |
|---|---|---|---|
| Announcements | 📢 | Announcement | Official RROS announcements, releases, updates and development milestones. |
| Questions & Support | ❓ | Question / Answer | Ask questions, receive technical support and share verified solutions. |
| Community Builds | 🤖 | Open-ended discussion | Share robot projects, Raspberry Pi builds, photos, videos and practical experiences. |
| Hardware & Compatibility | 🔧 | Open-ended discussion | Discuss components, hardware compatibility, electrical safety and documented test results. |
| Bugs & Troubleshooting | 🐛 | Question / Answer | Discuss errors and troubleshooting. Report reproducible defects through GitHub Issues. |
| Ideas & Feature Requests | 💡 | Open-ended discussion | Suggest and discuss new features, improvements and integrations. |
| Development & Contributions | 💻 | Open-ended discussion | Collaborate on RROS development, drivers, documentation and pull requests. |
| Polls | 📊 | Poll | Community polls and feedback on proposed project improvements. |

Announcement format restricts creating announcements to maintainers. Both
Question / Answer categories support questions, replies and accepted answers.
Polls uses GitHub's native poll format; GitHub does not support Discussion
forms for Polls. Native comments and supported attachments remain GitHub
features; form fields do not create an upload service.

Discussion forms are named for their category slugs. The prepared forms use
the expected slugs `questions-support`, `community-builds` and
`ideas-feature-requests`; on the future repository, verify the actual slugs
and move the files to match if GitHub generates different slugs. Verify each
form opens in its category and confirm answer-marking and Poll behavior in
the authenticated UI. Category configuration is **READY_TO_APPLY** only
after a repository exists and an authorized maintainer performs this UI
step.

## Automatic restoration preparation

The approved configuration above is machine-readable in
[discussion_categories.json](discussion_categories.json) and consumed by
`scripts/release/restore_discussions.py`. Validate the prepared files
offline at any time:

```sh
python scripts/release/restore_discussions.py --check
```

After the owner creates and authorizes the new repository, an authorized
maintainer runs:

```sh
python scripts/release/restore_discussions.py --repo Janus5G/RROS --apply
```

This enables Issues and Discussions, reads the live categories through the
documented GraphQL `discussionCategories` query, compares them with the
manifest (name, slug, emoji, format/answerability, exact description) and
reports the exact **Discussions → Categories → pencil** UI steps for each
category GitHub still requires a maintainer to create or edit manually. It
never creates repositories, never pushes, never deletes discussions or
categories and calls no undocumented endpoints. Rerun it until it reports
all required categories match; Polls remains optional.

Use Discussions for collaboration, Issues for reproducible defects and
Pull Requests for proposed code changes. Historical GitHub-hosted data
cannot be reconstructed from these repository files.

The [community guide](COMMUNITY.md) explains attachments, collaboration and
privacy. Review [evidence instructions](EVIDENCE.md) before sharing anything.
Current physical operations remain blocked, regardless of category.
