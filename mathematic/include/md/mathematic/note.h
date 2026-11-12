/// @file   note.h
/// @brief  An equal-tempered note: a MIDI number that knows its name, octave,
///         pitch class and frequency.

#pragma once

#include <md/mathematic/frequency.h>

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace md::mathematic {

	/// @brief  A note of the twelve-tone equal temperament.
	///
	/// The note is stored as a MIDI number, so transposition is integer addition
	/// and enharmonic spellings collapse to one value: "Bb3" and "A#3" are the
	/// same note. Names use sharps when printed.
	///
	/// @tparam Number  Floating-point type used when a frequency is requested.
	template<typename Number = double>
	class Note {
	private:
		long long midiValue;	///< MIDI note number, 69 for A4.

		/// @brief  Semitone offset of a note letter inside an octave.
		/// @param  letter  Upper-case letter between A and G.
		/// @return The offset from C, or -1 when the letter is not a note name.
		static constexpr int semitoneOf(const char letter) {
			switch (letter) {
			case 'C': return 0;
			case 'D': return 2;
			case 'E': return 4;
			case 'F': return 5;
			case 'G': return 7;
			case 'A': return 9;
			case 'B': return 11;
			default: return -1;
			}
		}

	public:
		/// @brief  Constructs middle C, MIDI 60.
		constexpr Note() : midiValue(60) {}

		/// @brief  Constructs a note from a MIDI number.
		/// @param  midi  MIDI note number; values outside 0-127 are accepted so
		///               that transposition and theory work stay unconstrained.
		constexpr explicit Note(const long long midi) : midiValue(midi) {}

		/// @brief  Creates a note from a MIDI number.
		static constexpr Note fromMidi(const long long midi) {
			return Note(midi);
		}

		/// @brief  Parses a note name such as "A4", "C#5", "Bb3" or "C-1".
		/// @param  text  Letter A-G, optional accidentals (# or b), then an octave.
		/// @return The parsed note.
		/// @throws std::invalid_argument  When the text is not a note name.
		static Note fromName(const std::string_view text) {
			std::size_t index = 0;
			if (index >= text.size()) {
				throw std::invalid_argument("Note::fromName: the name is empty");
			}

			const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[index])));
			const int base = semitoneOf(letter);
			if (base < 0) {
				throw std::invalid_argument("Note::fromName: the name must start with a letter from A to G");
			}
			++index;

			int accidental = 0;
			while (index < text.size() && (text[index] == '#' || text[index] == 'b')) {
				accidental += (text[index] == '#') ? 1 : -1;
				++index;
			}

			if (index >= text.size()) {
				throw std::invalid_argument("Note::fromName: the octave is missing");
			}

			int sign = 1;
			if (text[index] == '-') {
				sign = -1;
				++index;
			}
			if (index >= text.size()) {
				throw std::invalid_argument("Note::fromName: the octave is missing");
			}

			int octave = 0;
			for (; index < text.size(); ++index) {
				if (text[index] < '0' || text[index] > '9') {
					throw std::invalid_argument("Note::fromName: the octave must be an integer");
				}
				octave = octave * 10 + (text[index] - '0');
			}

			return Note(static_cast<long long>(12 * (sign * octave + 1) + base + accidental));
		}

		/// @brief  The nearest note to a frequency.
		/// @param  frequency  Frequency to round.
		/// @return The nearest equal-tempered note.
		static Note fromFrequency(const Frequency<Number>& frequency) {
			return Note(static_cast<long long>(frequency.nearestNote()));
		}

		/// @brief  The MIDI number.
		constexpr long long midi() const {
			return midiValue;
		}

		/// @brief  The frequency of the note.
		Frequency<Number> frequency() const {
			return Frequency<Number>::fromNote(static_cast<Number>(midiValue));
		}

		/// @brief  The pitch class, zero for C.
		constexpr long long pitchClass() const {
			const long long remainder = midiValue % 12;
			return remainder < 0 ? remainder + 12 : remainder;
		}

		/// @brief  The octave number, where middle C is C4.
		constexpr long long octave() const {
			// Subtract the pitch class first so the division is exact even for
			// negative MIDI numbers, where truncation would round the wrong way.
			return (midiValue - pitchClass()) / 12 - 1;
		}

		/// @brief  The name of the note, spelled with sharps.
		/// @return A name such as "C4", "A#3" or "C-1".
		std::string name() const {
			static const char* const names[12] = {
				"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
			};
			return std::string(names[pitchClass()]) + std::to_string(octave());
		}

		/// @brief  Returns the note transposed by a number of semitones.
		/// @param  semitones  Offset in semitones, positive to rise.
		constexpr Note transposed(const long long semitones) const {
			return Note(midiValue + semitones);
		}

		/// @brief  Equality test on the MIDI number, so enharmonic names agree.
		constexpr bool operator==(const Note& other) const {
			return midiValue == other.midiValue;
		}

		/// @brief  Inequality test.
		constexpr bool operator!=(const Note& other) const {
			return midiValue != other.midiValue;
		}

		/// @brief  Strict ordering by pitch.
		constexpr bool operator<(const Note& other) const {
			return midiValue < other.midiValue;
		}

		/// @brief  Strict ordering by pitch.
		constexpr bool operator>(const Note& other) const {
			return other.midiValue < midiValue;
		}

		/// @brief  Non-strict ordering by pitch.
		constexpr bool operator<=(const Note& other) const {
			return !(other < *this);
		}

		/// @brief  Non-strict ordering by pitch.
		constexpr bool operator>=(const Note& other) const {
			return !(*this < other);
		}
	};

	/// @brief  Writes a note to a stream by name.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Note<Number>& note) {
		stream << note.name();
		return stream;
	}
}
