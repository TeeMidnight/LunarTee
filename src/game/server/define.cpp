#include "define.h"
#include <string>

char* format_int64_with_commas(char commas, int64_t  n)
{
	char _number_array[64] = { '\0' };
	str_format(_number_array, sizeof(_number_array), "%ld", n); // %ll

	const char *_number_pointer = _number_array;
	int _number_of_digits = 0;
	while (*(_number_pointer + _number_of_digits++));
	--_number_of_digits;

	/*
	*	count the number of digits
	*	calculate the position for the first comma separator
	*	calculate the final length of the number with commas
	*
	*	the starting position is a repeating sequence 123123... which depends on the number of digits
	*	the length of the number with commas is the sequence 111222333444...
	*/
	const int _starting_separator_position = _number_of_digits < 4 ? 0 : _number_of_digits % 3 == 0 ? 3 : _number_of_digits % 3;
	const int _formatted_number_length = _number_of_digits + _number_of_digits / 3 - (_number_of_digits % 3 == 0 ? 1 : 0);

	// create formatted number array based on calculated information.
	char* _formatted_number = new char[20 * 3 + 1];

	// place all the commas
	for (int i = _starting_separator_position; i < _formatted_number_length - 3; i += 4)
		_formatted_number[i] = commas;

	// place the digits
	for (int i = 0, j = 0; i < _formatted_number_length; i++)
		if (_formatted_number[i] != commas)
			_formatted_number[i] = _number_pointer[j++];

	/* close the string */
	_formatted_number[_formatted_number_length] = '\0';
	return _formatted_number;
}