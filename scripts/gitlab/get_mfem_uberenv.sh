#!/bin/bash

set -o errexit
set -o nounset

uberenv_url="https://github.com/mfem/mfem-uberenv.git"
uberenv_ref="c2f3497e9a392885058dd2ba93e2f8c071655726"

[[ ! -d scripts/uberenv ]] && git clone ${uberenv_url} scripts/uberenv
cd scripts/uberenv
git fetch origin ${uberenv_ref}
git checkout ${uberenv_ref}
cd -
