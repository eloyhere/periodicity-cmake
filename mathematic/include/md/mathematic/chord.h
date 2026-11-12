/// @file   chord.h
/// @brief  A chord as a root note plus a set of semitone intervals, able to
///         render itself as notes, frequencies, tones or samples.

#pragma once

#include <md/mathematic/note.h>
#include <md/mathematic/tone.h>

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace md::mathematic {

	/// @brief  The chord qualities understood by Chord.
	enum class ChordQuality {
		Major,					///< Root, major third, perfect fifth.
		Minor,					///< Root, minor third, perfect fifth.
		Diminished,				///< Root, minor third, diminished fifth.
		Augmented,				///< Root, major third, augmented fifth.
		Sus2,					///< Root, major second, perfect fifth.
		Sus4,					///< Root, perfect fourth, perfect fifth.
		Power,					///< Root and perfect fifth only.
		DominantSeventh,		///< Major triad plus a minor seventh.
		MajorSeventh,			///< Major triad plus a major seventh.
		MinorSeventh,			///< Minor triad plus a minor seventh.
		DiminishedSeventh,		///< Diminished triad plus a diminished seventh.
		HalfDiminishedSeventh,	///< Diminished triad plus a minor seventh.
		MajorSixth,				///< Major triad plus a major sixth.
		MinorSixth				///< Minor triad plus a major sixth.
	};

	/// @brief  A chord: a root note and the semitone offsets of its members.
	///
	/// A chord carries no octave voicing beyond the root: member notes are the
	/// root transposed by each interval, so intervals may exceed an octave when a
	/// caller asks for them. Rendering to samples simply sums one Tone per member.
	///
	/// @tparam Number  Floating-point type used when frequencies or samples are
	///                 requested.
	template<typename Number = double>
	class Chord {
	private:
		Note<Number> rootValue;					///< Note the intervals are measured from.
		std::vector<long long> intervalValues;	///< Semitone offsets, zero for the root.
		ChordQuality qualityValue = ChordQuality::Major;	///< Named quality, for printing.
		bool custom = false;					///< True when the intervals were given explicitly.

		/// @brief  Canonical suffix of a quality, as used in chord symbols.
		static const char* suffixOf(const ChordQuality quality) {
			switch (quality) {
			case ChordQuality::Major: return "";
			case ChordQuality::Minor: return "m";
			case ChordQuality::Diminished: return "dim";
			case ChordQuality::Augmented: return "aug";
			case ChordQuality::Sus2: return "sus2";
			case ChordQuality::Sus4: return "sus4";
			case ChordQuality::Power: return "5";
			case ChordQuality::DominantSeventh: return "7";
			case ChordQuality::MajorSeventh: return "maj7";
			case ChordQuality::MinorSeventh: return "m7";
			case ChordQuality::DiminishedSeventh: return "dim7";
			case ChordQuality::HalfDiminishedSeventh: return "m7b5";
			case ChordQuality::MajorSixth: return "6";
			case ChordQuality::MinorSixth: return "m6";
			}
			return "";
		}

		/// @brief  The root name without its octave, for symbol printing.
		std::string rootName() const {
			std::string name = rootValue.name();
			while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back()))) {
				name.pop_back();
			}
			if (!name.empty() && name.back() == '-') {
				name.pop_back();
			}
			return name;
		}

	public:
		/// @brief  Constructs a C major chord in the fourth octave.
		Chord() : rootValue(Note<Number>::fromMidi(60)), intervalValues(intervalsOf(ChordQuality::Major)) {}

		/// @brief  Constructs a chord from explicit semitone intervals.
		/// @param  root       Root note.
		/// @param  intervals  Semitone offsets from the root; must not be empty.
		/// @throws std::invalid_argument  When @p intervals is empty.
		Chord(const Note<Number>& root, std::vector<long long> intervals)
			: rootValue(root), intervalValues(std::move(intervals)), custom(true) {
			if (intervalValues.empty()) {
				throw std::invalid_argument("Chord: a chord needs at least one interval");
			}
		}

		/// @brief  Constructs a chord of a named quality.
		/// @param  root     Root note.
		/// @param  quality  Chord quality.
		Chord(const Note<Number>& root, const ChordQuality quality)
			: rootValue(root), intervalValues(intervalsOf(quality)), qualityValue(quality) {}

		/// @brief  The semitone intervals of a named quality.
		/// @param  quality  Chord quality.
		/// @return The intervals, starting at zero.
		static std::vector<long long> intervalsOf(const ChordQuality quality) {
			switch (quality) {
			case ChordQuality::Major: return { 0, 4, 7 };
			case ChordQuality::Minor: return { 0, 3, 7 };
			case ChordQuality::Diminished: return { 0, 3, 6 };
			case ChordQuality::Augmented: return { 0, 4, 8 };
			case ChordQuality::Sus2: return { 0, 2, 7 };
			case ChordQuality::Sus4: return { 0, 5, 7 };
			case ChordQuality::Power: return { 0, 7 };
			case ChordQuality::DominantSeventh: return { 0, 4, 7, 10 };
			case ChordQuality::MajorSeventh: return { 0, 4, 7, 11 };
			case ChordQuality::MinorSeventh: return { 0, 3, 7, 10 };
			case ChordQuality::DiminishedSeventh: return { 0, 3, 6, 9 };
			case ChordQuality::HalfDiminishedSeventh: return { 0, 3, 6, 10 };
			case ChordQuality::MajorSixth: return { 0, 4, 7, 9 };
			case ChordQuality::MinorSixth: return { 0, 3, 7, 9 };
			}
			return { 0, 4, 7 };
		}

		/// @brief  Parses a chord symbol such as "C", "Am", "F#m7" or "G7".
		/// @param  symbol  Root name followed by an optional quality suffix.
		/// @return The parsed chord.
		/// @throws std::invalid_argument  When the root or the suffix is unknown.
		/// @note   Chord symbols carry no octave, so the root is always placed in
		///         the fourth octave: "G7" is G4 with a minor seventh, never the
		///         note G7. Ask for a different register by constructing the chord
		///         from a Note and a quality instead.
		static Chord fromSymbol(const std::string_view symbol) {
			if (symbol.empty()) {
				throw std::invalid_argument("Chord::fromSymbol: the symbol is empty");
			}

			std::size_t index = 0;
			if (!std::isalpha(static_cast<unsigned char>(symbol[index]))) {
				throw std::invalid_argument("Chord::fromSymbol: the root must start with a letter");
			}
			++index;
			while (index < symbol.size() && (symbol[index] == '#' || symbol[index] == 'b')) {
				++index;
			}

			// Only the letter and its accidentals belong to the root; every digit
			// that follows is part of the quality, so "G7" stays a G chord.
			const std::string rootText = std::string(symbol.substr(0, index)) + '4';
			const std::string suffix(symbol.substr(index));
			return Chord(Note<Number>::fromName(rootText), qualityOf(suffix));
		}

		/// @brief  The quality that a symbol suffix names.
		/// @param  suffix  Text after the root, empty for a major triad.
		/// @return The matching quality.
		/// @throws std::invalid_argument  When the suffix is not understood.
		static ChordQuality qualityOf(const std::string_view suffix) {
			if (suffix.empty() || suffix == "maj") {
				return ChordQuality::Major;
			}
			if (suffix == "m" || suffix == "min") {
				return ChordQuality::Minor;
			}
			if (suffix == "dim") {
				return ChordQuality::Diminished;
			}
			if (suffix == "aug" || suffix == "+") {
				return ChordQuality::Augmented;
			}
			if (suffix == "sus2") {
				return ChordQuality::Sus2;
			}
			if (suffix == "sus4" || suffix == "sus") {
				return ChordQuality::Sus4;
			}
			if (suffix == "5") {
				return ChordQuality::Power;
			}
			if (suffix == "7") {
				return ChordQuality::DominantSeventh;
			}
			if (suffix == "maj7") {
				return ChordQuality::MajorSeventh;
			}
			if (suffix == "m7") {
				return ChordQuality::MinorSeventh;
			}
			if (suffix == "dim7") {
				return ChordQuality::DiminishedSeventh;
			}
			if (suffix == "m7b5") {
				return ChordQuality::HalfDiminishedSeventh;
			}
			if (suffix == "6") {
				return ChordQuality::MajorSixth;
			}
			if (suffix == "m6") {
				return ChordQuality::MinorSixth;
			}
			throw std::invalid_argument("Chord::qualityOf: unknown chord suffix");
		}

		/// @brief  The root note.
		constexpr const Note<Number>& root() const {
			return rootValue;
		}

		/// @brief  The semitone intervals.
		const std::vector<long long>& intervals() const {
			return intervalValues;
		}

		/// @brief  Number of members.
		std::size_t size() const {
			return intervalValues.size();
		}

		/// @brief  The member notes, one per interval.
		std::vector<Note<Number>> notes() const {
			std::vector<Note<Number>> result;
			result.reserve(intervalValues.size());
			for (const long long interval : intervalValues) {
				result.push_back(rootValue.transposed(interval));
			}
			return result;
		}

		/// @brief  The member frequencies in hertz.
		std::vector<Frequency<Number>> frequencies() const {
			std::vector<Frequency<Number>> result;
			result.reserve(intervalValues.size());
			for (const long long interval : intervalValues) {
				result.push_back(rootValue.transposed(interval).frequency());
			}
			return result;
		}

		/// @brief  One Tone per member, all at the same amplitude.
		/// @param  amplitude  Linear amplitude of every member.
		/// @return The tones, in interval order.
		std::vector<Tone<Number>> tones(const Number& amplitude = One<Number>) const {
			std::vector<Tone<Number>> result;
			result.reserve(intervalValues.size());
			for (const long long interval : intervalValues) {
				result.push_back(Tone<Number>(rootValue.transposed(interval).frequency(), amplitude));
			}
			return result;
		}

		/// @brief  Renders the chord as the sum of its members.
		/// @param  rate       Sampling rate of the output.
		/// @param  count      Number of samples.
		/// @param  startTime  Time of the first sample, in seconds.
		/// @return count samples of the sum of the member tones.
		/// @note   No normalisation is applied; three members at unit amplitude can
		///         reach three, so callers that need headroom should scale.
		std::vector<Number> samples(const SampleRate<Number>& rate, const std::size_t count,
			const Number& startTime = Zero<Number>) const {
			std::vector<Number> result(count, Zero<Number>);
			for (const Tone<Number>& tone : tones()) {
				const std::vector<Number> member = tone.samples(rate, count, startTime);
				for (std::size_t index = 0; index < count; ++index) {
					result[index] += member[index];
				}
			}
			return result;
		}

		/// @brief  The chord symbol, such as "C", "Am7" or "F#dim".
		/// @return The symbol; explicitly constructed intervals print as
		///         "root[0, 4, 7]".
		std::string symbol() const {
			if (custom) {
				std::string text = rootName() + "[";
				for (std::size_t index = 0; index < intervalValues.size(); ++index) {
					if (index != 0) {
						text += ", ";
					}
					text += std::to_string(intervalValues[index]);
				}
				return text + "]";
			}
			return rootName() + suffixOf(qualityValue);
		}

		/// @brief  Equality test on the root and the intervals.
		bool operator==(const Chord& other) const {
			return rootValue == other.rootValue && intervalValues == other.intervalValues;
		}

		/// @brief  Inequality test.
		bool operator!=(const Chord& other) const {
			return !(*this == other);
		}
	};

	/// @brief  Writes a chord to a stream by symbol.
	template<typename Number>
	std::ostream& operator<<(std::ostream& stream, const Chord<Number>& chord) {
		stream << chord.symbol();
		return stream;
	}
}
