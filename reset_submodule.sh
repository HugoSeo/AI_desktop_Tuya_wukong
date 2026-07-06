#!/bin/bash

# 一键重置所有子模块
echo "重置所有Git子模块..."
#!/bin/bash

# 获取子模块信息并重置
git config --file .gitmodules --get-regexp "submodule\." | \
while read key value; do
    if [[ $key == *.path ]]; then
        # 提取子模块名和路径
        name=$(echo $key | sed 's/submodule\.\(.*\)\.path/\1/')
        path=$value
        # 获取对应的URL
        url=$(git config --file .gitmodules --get "submodule.$name.url")
        # 获取分支（如果有）
        branch=$(git config --file .gitmodules --get "submodule.$name.branch" 2>/dev/null)
        
        echo "重置: $path -> $url"
        
        # 清理
        git submodule deinit -f "$path" 2>/dev/null || true
        rm -rf "$path" ".git/modules/$path" 2>/dev/null || true
        
        # 重新添加
        if [ -n "$branch" ]; then
            git submodule add -b "$branch" "$url" "$path"
        else
            git submodule add "$url" "$path"
        fi
        
        # 更新
        git submodule update --init --recursive "$path"
        git submodule update --remote "$path" 2>/dev/null || true
        
        echo "✓ $path 完成"
    fi
done