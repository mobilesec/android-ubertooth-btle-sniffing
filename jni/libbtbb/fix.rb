#!/usr/bin/ruby

IO.readlines("sw_check_tables.h").each do |line|
  puts line.gsub(",", "ULL,").gsub("}","ULL}")
end
