#!/bin/bash

declare -a upstreams
upstreams=(libecl
           opm-common)

declare -A upstreamRev
upstreamRev[opm-common]=master
upstreamRev[libecl]=master

if grep -q "opm-common=" <<< $ghprbCommentBody
then
  upstreamRev[opm-common]=pull/`echo $ghprbCommentBody | sed -r 's/.*opm-common=([0-9]+).*/\1/g'`/merge
fi

# Downstreams and revisions
declare -a downstreams
downstreams=(opm-material
             opm-models
             opm-simulators
             opm-upscaling)

declare -A downstreamRev
downstreamRev[opm-material]=master
downstreamRev[opm-models]=master
downstreamRev[opm-simulators]=master
downstreamRev[opm-upscaling]=master

# Clone opm-common
pushd .
mkdir -p $WORKSPACE/deps/opm-common
cd $WORKSPACE/deps/opm-common
git init .
git remote add origin https://github.com/OPM/opm-common
git fetch --depth 1 origin ${upstreamRev[opm-common]}:branch_to_build
test $? -eq 0 || exit 1
git checkout branch_to_build
popd

source $WORKSPACE/deps/opm-common/jenkins/build-opm-module.sh

parseRevisions
printHeader opm-grid

# Setup opm-data
if grep -q "with downstreams" <<< $ghprbCommentBody
then
  source $WORKSPACE/deps/opm-common/jenkins/setup-opm-tests.sh
fi

build_module_full opm-grid
