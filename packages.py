import subprocess
import re
import os

app_name = "wxapp.exe"

def get_msys2_dlls(executable):
    # 执行 ldd 命令
    try:
        result = subprocess.run(['ldd', executable], capture_output=True, text=True, check=True)
        dlls = result.stdout.splitlines()
    except subprocess.CalledProcessError as e:
        print(f"Error executing ldd: {e}")
        return []

    # 对结果进行划分
    msys2_dlls = []
    for line in dlls:
        line = line.split("=>")[1].strip()
        # 去掉 后的内容，以空格为分隔，取第一段
        line = line.split(" ")[0]
        # 去掉以/c/开头的行
        if line.startswith("/c/"):
            continue
        msys2_dlls.append(line)
    return msys2_dlls

if __name__ == "__main__":
    executable = app_name  # 替换为您的可执行文件名
    msys2_dlls = get_msys2_dlls(executable)
    
    print("MSYS2 环境的 DLL 列表:")
    # 创建dict目录
    dict_dir = "dict"
    if not os.path.exists(dict_dir):
        os.makedirs(dict_dir)
    for dll in msys2_dlls:
        print(dll)
        # 拷贝此dll到当前目录的dict目录下
        subprocess.run(['cp', dll, dict_dir])
    # 将app.exe拷贝到dict目录下
    subprocess.run(['cp', app_name, dict_dir])
    # 将icon.ico拷贝到dict目录下
    subprocess.run(['cp', 'icon.ico', dict_dir])

