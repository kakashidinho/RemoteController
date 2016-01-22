@echo off

pushd %~dp1

copy /b %~nx1 +,,

popd