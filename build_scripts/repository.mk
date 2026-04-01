## Repository assembly ##########################################################
#
# Variables (all overridable via environment or CLI):
#   REPO_DIR          — path to the repository root; MUST be supplied (no default)
#                       Typically a git worktree checked out to the 'repository' branch:
#                         git worktree add /tmp/keen-pbr-repo repository
#   REPO_PREFIX       — channel prefix inside the repo, e.g. "local", "unstable", "stable"
#   KEENETIC_VERSION  — Keenetic channel subdirectory, e.g. "current"
#   DEBIAN_VERSION    — Debian distribution name, e.g. "bookworm"

REPO_PREFIX      ?= local
KEENETIC_VERSION ?= current
DEBIAN_VERSION   ?= bookworm

.PHONY: build-repository

build-repository: ## Assemble repository tree from build/packages/ into REPO_DIR/REPO_PREFIX/
	@test -n "$(REPO_DIR)" || { \
	  echo "ERROR: REPO_DIR is required."; \
	  echo "  Set up a worktree first:  git worktree add /tmp/keen-pbr-repo repository"; \
	  echo "  Then run:                 make build-repository REPO_DIR=/tmp/keen-pbr-repo"; \
	  exit 1; \
	}
	bash build_scripts/build-repository.sh \
	  build/packages "$(REPO_DIR)" "$(REPO_PREFIX)" \
	  "$(KEENETIC_VERSION)" "$(DEBIAN_VERSION)"
