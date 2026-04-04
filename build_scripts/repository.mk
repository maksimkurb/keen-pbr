## Repository assembly ##########################################################
#
# Variables (all overridable via environment or CLI):
#   REPO_DIR          — path to the repository root; MUST be supplied (no default)
#                       Typically a git worktree checked out to the 'repository' branch:
#                         git worktree add /tmp/keen-pbr-repo repository
#   REPO_TARGET_ROOT  — target root inside the repository branch,
#                       e.g. "stable" or "feature_unify_packaging"
#   REPO_BASE_PATH    — optional publish-path prefix; if it starts with
#                       "repository/", that prefix is omitted from public URLs
#   REPO_PUBLIC_BASE_URL — public base URL for generated repository links
#                          (default: "https://repo.keen-pbr.fyi")

REPO_TARGET_ROOT ?= local

.PHONY: build-repository

build-repository: ## Replace REPO_DIR/REPO_TARGET_ROOT/ with build/packages/{openwrt,keenetic,debian}
	@test -n "$(REPO_DIR)" || { \
	  echo "ERROR: REPO_DIR is required."; \
	  echo "  Set up a worktree first:  git worktree add /tmp/keen-pbr-repo repository"; \
	  echo "  Then run:                 make build-repository REPO_DIR=/tmp/keen-pbr-repo"; \
	  exit 1; \
	}
	bash build_scripts/build-repository.sh \
	  build/packages "$(REPO_DIR)" "$(REPO_TARGET_ROOT)"
