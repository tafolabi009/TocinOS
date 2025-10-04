#!/usr/bin/env python3
"""
TocinOS License Header Tool
Adds license headers to source files that don't have them
"""

import os
import sys
import re

LICENSE_HEADER_C = """/**
 * TocinOS - Modern Operating System
 * Copyright (C) 2024 TocinOS Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

"""

LICENSE_HEADER_ASM = """; TocinOS - Modern Operating System
; Copyright (C) 2024 TocinOS Team
; 
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.

"""

def has_license(content):
    """Check if file already has a license header"""
    return 'Copyright' in content[:500] or 'TocinOS Team' in content[:500]

def add_license_to_file(filepath, dry_run=False):
    """Add license header to a single file"""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        
        if has_license(content):
            return False
        
        # Choose appropriate license header
        if filepath.endswith('.asm'):
            header = LICENSE_HEADER_ASM
        else:
            header = LICENSE_HEADER_C
        
        # Add header
        new_content = header + content
        
        if not dry_run:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
        
        return True
    except Exception as e:
        print(f"Error processing {filepath}: {e}", file=sys.stderr)
        return False

def process_directory(directory, extensions, dry_run=False):
    """Process all files in directory with given extensions"""
    count = 0
    
    for root, dirs, files in os.walk(directory):
        # Skip build and test directories
        dirs[:] = [d for d in dirs if d not in ['build', 'docs', '.git']]
        
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                filepath = os.path.join(root, file)
                if add_license_to_file(filepath, dry_run):
                    count += 1
                    print(f"{'[DRY-RUN] ' if dry_run else ''}Added license to: {filepath}")
    
    return count

def main():
    """Main function"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Add license headers to source files')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be done')
    parser.add_argument('--dir', default='.', help='Directory to process (default: current)')
    args = parser.parse_args()
    
    print("TocinOS License Header Tool")
    print("=" * 50)
    
    # Process C/C++ files
    c_extensions = ['.c', '.h', '.cpp', '.hpp']
    c_count = process_directory(os.path.join(args.dir, 'kernel'), c_extensions, args.dry_run)
    c_count += process_directory(os.path.join(args.dir, 'include'), c_extensions, args.dry_run)
    
    # Process assembly files
    asm_extensions = ['.asm', '.s']
    asm_count = process_directory(os.path.join(args.dir, 'boot'), asm_extensions, args.dry_run)
    asm_count += process_directory(os.path.join(args.dir, 'kernel'), asm_extensions, args.dry_run)
    
    print("=" * 50)
    print(f"Total files processed: {c_count + asm_count}")
    if args.dry_run:
        print("(This was a dry run - no files were modified)")

if __name__ == '__main__':
    main()
