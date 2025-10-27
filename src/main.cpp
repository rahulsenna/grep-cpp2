#include <iostream>
#include <string>
#include <vector>
#include <sstream>

enum PatternType
{

  CHAR = 0x0,
  DIGIT,
  W_CHAR,
  PLUS,
  STAR,
  OPTIONAL,
  OR,
  CHAR_GROUP_POSITIVE,
  CHAR_GROUP_NEGATIVE,
  BACK_REF,
  NONE,
  COUNT
};

typedef struct Pattern
{
  PatternType type;
  PatternType quantifier;
	char c_char;
	char p_char;
	char n_char;
  std::vector<Pattern> group;
  std::string char_group;
} Pattern;

Pattern parse_single_pattern(std::string pattern, int &idx)
{
  Pattern result = {};
  result.type = CHAR;
  result.quantifier = NONE;
  result.c_char = pattern[idx];
  result.n_char = 0;
  result.p_char = 0;
  if (idx - 1 >= 0)
    result.p_char = pattern[idx - 1];
  if (idx + 1 < pattern.length())
    result.n_char = pattern[idx + 1];

  switch (result.c_char)
  {
    case '\\': 
    {
      idx++;
      switch (pattern[idx])
      {
      case 'd': result.type = DIGIT; break;
      case 'w': result.type = W_CHAR; break;
      default: break;
      }
      break;
    }
    case '[':
    {
      result.type = CHAR_GROUP_POSITIVE;
      idx++;
      if (pattern[idx] == '^')
      { 
      	result.type = CHAR_GROUP_NEGATIVE;
        idx++;
      }
      std::ostringstream ss;
      char prev = 0;
      while(pattern[idx] != ']')
      {
        if (pattern[idx] == '-' && prev != 0)
        {
          idx++;
          while(prev < pattern[idx])
          {
            ss << prev;
            prev++;
          }
        }
        prev = pattern[idx];
        ss << prev;
        idx++;
      }
      result.char_group = ss.str();
    }

    default: break;
  }

  switch (result.n_char)
  {
    case '+': result.quantifier = PLUS ; break;
    case '*': result.quantifier = STAR; break;
    case '?': result.quantifier = OPTIONAL; break;
    case '|': result.quantifier = OR; break;
    default: break;
  }
  idx++;
  return result;
};

std::vector<Pattern> parse_whole_pattern(std::string pattern)
{
  std::vector<Pattern> res;
  for (int i = 0; i < pattern.length();)
  {
    res.push_back(parse_single_pattern(pattern, i));
  }
  return res;
}
bool match_single(Pattern pattern, char chr)
{
  if (pattern.type == CHAR)
  {
    if (pattern.c_char != chr && (pattern.quantifier != STAR || pattern.quantifier != OPTIONAL))
    {
      return false;
    }
  }
  else if (pattern.type == DIGIT)
  {
    if (!isdigit(chr))
    {
      return false;
    }
  }
  else if (pattern.type == W_CHAR)
  {
    if (!isalnum(chr) && chr != '_')
    {
      return false;
    }
  }
  else if (pattern.type == CHAR_GROUP_POSITIVE)
  {
    return pattern.char_group.contains(chr);
  }
  else if (pattern.type == CHAR_GROUP_NEGATIVE)
  {
    return !pattern.char_group.contains(chr);
  }

  return true;
}

bool match_curr_pattern(Pattern pattern, const std::string &input, int &idx)
{
  char chr = input[idx++];
  
  if (!match_single(pattern, chr)) 
  {
  	return false;
  }
  
  if (pattern.quantifier == STAR && pattern.c_char == chr)
  {
    while (idx < input.length() && input[idx] == pattern.c_char)
      idx++;
  }
  else if (pattern.quantifier == PLUS)
  {
    while (idx < input.length() && input[idx] == pattern.c_char)
      idx++;
  }
  return true;
}

bool match_pattern(const std::string &input_line, const std::string &pattern_text)
{
  auto patterns = parse_whole_pattern(pattern_text);
  int pattern_len = patterns.size();
  int input_len = input_line.size();
  bool found_beg = false;
  int i = 0, pi = 0;
  for (; i < input_len && pi < pattern_len;)
  {
    if (match_curr_pattern(patterns[pi], input_line, i))
    { 
    	found_beg = true;
      pi++;
    } else
    {
      if (found_beg)
        return false;
    }
  }
  while (pi < pattern_len)
  { 
  	if (!(patterns[pi].quantifier == STAR || patterns[pi].quantifier == OPTIONAL))
  		return false;
  }
  return found_beg;
}

int main(int argc, char *argv[])
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cerr << "Logs from your program will appear here" << std::endl;

  if (argc != 3)
  {
    std::cerr << "Expected two arguments" << std::endl;
    return 1;
  }

  std::string flag = argv[1];
  std::string pattern = argv[2];

  if (flag != "-E")
  {
    std::cerr << "Expected first argument to be '-E'" << std::endl;
    return 1;
  }


#if 1 // DEBUG
  std::string input_line;
  std::getline(std::cin, input_line);
#else
  std::string input_line = "p";
#endif
  try
  {
    if (match_pattern(input_line, pattern))
    {
      return 0;
    }
    else
    {
      return 1;
    }
  }
  catch (const std::runtime_error &e)
  {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
