import json
import sys
from typing import Any

def format_json_with_simple_arrays_inline(data: Any, indent: int = 2) -> str:
    """
    将JSON格式化为字符串，简单数组（无嵌套）保持在一行
    
    Args:
        data: 要格式化的JSON数据
        indent: 缩进空格数
    
    Returns:
        格式化后的JSON字符串
    """
    def _format(value: Any, level: int = 0, is_array_element: bool = False) -> str:
        """递归格式化JSON数据"""
        indent_str = ' ' * (indent * level)
        next_indent = ' ' * (indent * (level + 1))
        
        if isinstance(value, dict):
            if not value:
                return '{}'
            
            # 格式化字典
            items = []
            for k, v in value.items():
                # 递归格式化值
                formatted_v = _format(v, level + 1)
                items.append(f'{next_indent}"{k}": {formatted_v}')
            
            return '{\n' + ',\n'.join(items) + '\n' + indent_str + '}'
        
        elif isinstance(value, list):
            if not value:
                return '[]'
            
            # 检查是否包含嵌套结构
            has_nested = any(isinstance(item, (dict, list)) for item in value)
            
            if has_nested:
                # 有嵌套结构，保持多行格式
                items = []
                for item in value:
                    formatted_item = _format(item, level + 1)
                    items.append(next_indent + formatted_item)
                
                return '[\n' + ',\n'.join(items) + '\n' + indent_str + ']'
            else:
                # 没有嵌套结构，全部放在一行
                items = []
                for item in value:
                    if isinstance(item, str):
                        # 转义字符串中的特殊字符
                        items.append(json.dumps(item, ensure_ascii=False))
                    else:
                        items.append(json.dumps(item, ensure_ascii=False))
                
                return '[' + ', '.join(items) + ']'
        
        elif isinstance(value, str):
            # 字符串需要转义
            return json.dumps(value, ensure_ascii=False)
        
        else:
            # 其他类型（数字、布尔值、None等）
            return json.dumps(value, ensure_ascii=False)
    
    return _format(data)

def process_json_file(input_file: str, output_file: str = None, indent: int = 2):
    """
    处理JSON文件，将简单数组格式化为一行
    
    Args:
        input_file: 输入文件路径
        output_file: 输出文件路径（如果不指定，则覆盖原文件）
        indent: 缩进空格数
    """
    try:
        # 读取JSON文件
        with open(input_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        # 格式化JSON
        formatted_json = format_json_with_simple_arrays_inline(data, indent)
        
        # 确定输出文件
        if output_file is None:
            output_file = input_file
        
        # 写入处理后的JSON
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(formatted_json)
            if not formatted_json.endswith('\n'):
                f.write('\n')
        
        print(f"✅ 处理完成！输出文件: {output_file}")
        
    except json.JSONDecodeError as e:
        print(f"❌ JSON解析错误: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"❌ 发生错误: {e}")
        sys.exit(1)

def main():
    """命令行入口"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='格式化JSON文件，将不含嵌套结构的数组保持在一行',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  python json_formatter.py input.json
  python json_formatter.py input.json -o output.json
  python json_formatter.py input.json -i 4
        """
    )
    
    parser.add_argument(
        'input_file',
        help='输入的JSON文件路径'
    )
    
    parser.add_argument(
        '-o', '--output',
        help='输出文件路径（默认覆盖原文件）'
    )
    
    parser.add_argument(
        '-i', '--indent',
        type=int,
        default=2,
        help='缩进空格数（默认2）'
    )
    
    args = parser.parse_args()
    process_json_file(args.input_file, args.output, args.indent)

if __name__ == '__main__':
    main()