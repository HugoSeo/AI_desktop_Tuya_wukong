#!/bin/sh

# CI系统传入的参数说明：
#
# $1 - 组件项目名称，如： linux-ubuntu-6.2.0_64Bit
# $2 - 组件构建版本，如： 2.3.3-beta.86
# $3 - 组件打包路径，如： output/dist/linux-ubuntu-6.2.0_64Bit_2.3.3-beta.86.zip
#
# 例：
# ./ci_autopack.sh linux-ubuntu-6.2.0_64Bit 2.3.3-beta.86 output/dist/linux-ubuntu-6.2.0_64Bit_2.3.3-beta.86.zip

set -e

cd `dirname $0`

PRJ_DIR=$(pwd)
PRJ_NAME=$(basename "$PRJ_DIR")

# 使用临时文件
TEMP_OUTPUT="/tmp/${PRJ_NAME}_$(date +%s).tar.gz"
FINAL_OUTPUT="$PRJ_DIR/$3"

echo "项目名称: $PRJ_NAME"
echo "临时输出: $TEMP_OUTPUT"
echo "最终输出: $FINAL_OUTPUT"

# update submodules
git submodule update --init --recursive

# make zip
cd ..
tar -zcf "$TEMP_OUTPUT" \
    --exclude "${PRJ_NAME}/output" \
    --exclude "${PRJ_NAME}/.git" \
    --exclude "${PRJ_NAME}/.gitignore" \
    --exclude "${PRJ_NAME}/.gitmodules" \
    --exclude "${PRJ_NAME}/.cursor" \
    "${PRJ_NAME}/"

# 移动到最终位置
mkdir -p `dirname "$FINAL_OUTPUT"`
mv "$TEMP_OUTPUT" "$FINAL_OUTPUT"

echo "✓ 打包完成: $FINAL_OUTPUT"
ls -lh "$FINAL_OUTPUT"
