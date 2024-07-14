#include <iostream>
#include <fstream>
#include <vector>
#include "macho.h"

struct dwarf_section {
  std::string name;
  uint64_t address;
  uint64_t offset;
  uint64_t offset_old;
  uint64_t size;
};

void fs_copy_some(std::ifstream& input_fs, std::ofstream& output_fs, uint64_t size) {
  if (size <= 0)
    return;

  constexpr int chunk_size = 0x1000000;
  const auto buf = std::make_shared<char[]>(chunk_size);
  const auto cycle = size / chunk_size;
  const auto remain = size % chunk_size;
  for (int i = 0; i < cycle; i++) {
    input_fs.read(buf.get(), chunk_size);
    output_fs.write(buf.get(), chunk_size);
  }
  if (remain) {
    input_fs.read(buf.get(), remain);
    output_fs.write(buf.get(), remain);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cout << "usage: fix-ios-dwarf <input> <output>" << std::endl;
    return 1;
  }

  const auto input_path = argv[1];
  const auto output_path = argv[2];
  std::ifstream input_fs(input_path, std::ios::binary);

  mach_header_64 mach_header{};
  input_fs.read((char*)&mach_header, sizeof(mach_header));

  switch (mach_header.magic) {
    case FAT_MAGIC:
      std::cout << "does not support fat mach-o" << std::endl;
      return 1;
    case MH_MAGIC:
      std::cout << "does not support 32-bit mach-o" << std::endl;
      return 1;
    case MH_MAGIC_64:
      break;
    default:
      std::cout << "unknown file type" << std::endl;
      return 1;
  }

  std::vector<dwarf_section> dwarf_section_list;
  uint64_t dwarf_seg_address, dwarf_seg_offset;
  uint64_t dwarf_section_header_offset;
  uint64_t dwarf_section_data_offset, dwarf_section_data_offset_last;

  for (int i = 0; i < mach_header.ncmds; i++) {
    segment_command_64 command{};
    input_fs.read((char*)&command, sizeof(load_command));
    if (command.cmd != LC_SEGMENT_64) {
      input_fs.ignore(command.cmdsize - sizeof(load_command));
      continue;
    }
    input_fs.read((char*)&command + sizeof(load_command), sizeof(segment_command_64) - sizeof(load_command));
    if (command.segname == std::string("__DWARF")) {
      if (i != mach_header.ncmds - 1) {
        std::cout << "__DWARF segment is not at the end of the Mach-O file" << std::endl;
        return 1;
      }

      dwarf_seg_address = command.vmaddr;
      dwarf_seg_offset = command.fileoff;
      dwarf_section_header_offset = input_fs.tellg();
      dwarf_section_data_offset = dwarf_seg_offset;
      dwarf_section_data_offset_last = dwarf_section_data_offset;
      for (int j = 0; j < command.nsects; j++) {
        section_64 section{};
        input_fs.read((char*)&section, sizeof(section));
        // magic happens here: calculate the REAL offset using address
        const auto offset = section.addr - dwarf_seg_address + dwarf_seg_offset;
        dwarf_section_list.push_back({section.sectname, 0, 0, offset, section.size});
      }
    } else {
      input_fs.ignore(command.cmdsize - sizeof(segment_command_64));
      continue;
    }
  }

  for (int i = 0; i < dwarf_section_list.size(); i++) {
    const auto section = dwarf_section_list[i];
    if (section.name == "__debug_info") {
      if (i == dwarf_section_list.size() - 1) {
        std::cout << "__debug_info section is already at the end of the __DWARF segment" << std::endl;
        return 1;
      }

      dwarf_section_list.erase(dwarf_section_list.begin() + i);
      dwarf_section_list.push_back(section);
      break;
    }
  }
  for (auto& section : dwarf_section_list) {
    section.offset = dwarf_section_data_offset_last;
    section.address = section.offset + dwarf_seg_address - dwarf_seg_offset;
    dwarf_section_data_offset_last += section.size;
  }

  std::ofstream output_fs(output_path, std::ios::binary);
  std::cout << "fixing..." << std::endl;

  input_fs.seekg(0);
  fs_copy_some(input_fs, output_fs, dwarf_section_header_offset);

  for (auto& section : dwarf_section_list) {
    section_64 macho_section{};
    macho_section.addr = section.address;
    macho_section.offset = section.offset;
    macho_section.size = section.size;
    snprintf(macho_section.segname, sizeof(macho_section.segname), "__DWARF");
    snprintf(macho_section.sectname, sizeof(macho_section.sectname), "%s", section.name.c_str());
    output_fs.write((char*)&macho_section, sizeof(macho_section));
  }

  const auto intersection_size = dwarf_section_data_offset - input_fs.tellg() - sizeof(section_64) * dwarf_section_list.size();
  fs_copy_some(input_fs, output_fs, intersection_size);

  for (auto& section : dwarf_section_list) {
    input_fs.seekg(section.offset_old);
    fs_copy_some(input_fs, output_fs, section.size);
  }

  input_fs.seekg(0, std::ios::end);
  const auto left_size = (uint64_t)input_fs.tellg() - dwarf_section_data_offset_last;
  if (left_size > 0) {
    input_fs.seekg(dwarf_section_data_offset_last);
    fs_copy_some(input_fs, output_fs, left_size);
  }

  input_fs.close();
  output_fs.close();

  std::cout << "done, output file: " << output_path << std::endl;

  return 0;
}
