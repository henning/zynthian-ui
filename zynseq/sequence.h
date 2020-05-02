#pragma once
#include "pattern.h"
#include <map>
#include <forward_list>

struct SEQ_EVENT
{
	uint32_t time;
	MIDI_MESSAGE msg;
};

/**	Sequence class provides an arbritary quantity of non-overlapping patterns. The sequence has a player which feeds events to a JACK client.
*/
class Sequence
{
	public:
		/**	@brief	Construct a Sequence object
		*	@param	tracks Quantity of tracks
		*/
		Sequence();

		/**	@brief	Destroy a Sequence object
		*/
		~Sequence();

		/**	@brief	Add pattern to sequence
		*	@param	position Quantity of clock cycles from start of sequence at which to add pattern
		*	@param	pattern Pointer to pattern to add
		*	@note	Overlapping patterns will be removed
		*/
		void addPattern(uint32_t position, Pattern* pattern);

		/**	@brief	Remove pattern from sequence
		*	@param	position Quantity of clock cycles from start of sequence at which pattern starts
		*/
		void removePattern(uint32_t position);

		/**	@brief	Get pattern
		*	@param	position Quantity of clock cycles from start of sequence at which pattern starts
		*	@retval	Pattern* Pointer to pattern or NULL if no pattern starts at this position
		*/
		Pattern* getPattern(uint32_t position);

		/**	@brief	Get pattern
		*	@param index Index of pattern within table
		*	@retval Pattern* Pointer to pattern or NULL if invalid index
		*/
		Pattern* getPatternAt(uint32_t index);

		/**	@brief	Get MIDI channel
		*	@retval	uint8_t MIDI channel
		*/
		uint8_t getChannel();

		/**	@brief	Set MIDI channel
		*	@param	channel MIDI channel
		*/
		void setChannel(uint8_t channel);

		/**	@brief	Get JACK output
		*	@retval	uint8_t JACK output number
		*/
		uint8_t getOutput();

		/**	@brief	Set Jack output
		*	@param	output JACK output index
		*/
		void setOutput(uint8_t output);

		/**	@brief	Get play mode
		*	@retval	uint8_t Play mode
		*/
		uint8_t getPlayMode();

		/**	@brief	Set play mode
		*	@param	mode Play mode [STOP | PLAY | LOOP]
		*/
		void setPlayMode(uint8_t mode);

		/**	@brief	Toggles play / stop
		*/
		void togglePlayMode();

		/**	@brief	Handle clock signal
		*	@param	nTime Time (quantity of samples since JACK epoch)
		*	@retval	bool True if clock triggers a sequence step
		*	@note	Adds pending events from sequence to JACK queue
		*/
		bool clock(uint32_t nTime);

		/**	@brief	Gets next event at current clock cycle
		*	@retval	SEQ_EVENT* Pointer to sequence event at this time or NULL if no more events
		*	@note	Start, end and interpolated events are returned on each call. Time is offset from start of clock cycle in samples.
		*/
		SEQ_EVENT* getEvent();

		/**	@brief	Update length of sequence by iterating through all patterns to find last clock cycle
		*/
		void updateLength();

		/**	@brief	Get duration of sequence in clock cycles
		*	@retval	uint32_t Length of sequence in clock cycles
		*/
		uint32_t getLength();

		/**	@brief	Remove all patterns from sequence
		*/
		void clear();

		/**	@brief	Get position of playhead within currently playing pattern
		*	@retval	uint32_t Quantity of steps from start of pattern to playhead
		*/
		uint32_t getStep();

		/**	@brief	Get position of playhead within currently playing pattern
		*	@retval	uint32_t Quantity of clock cycles from start of pattern to playhead
		*/
		uint32_t getPatternPlayhead();

		/**	@brief	Get the position of playhead within sequence
		*	@retval	uint32_t Quantity of clock cycles from start of sequence to playhead
		*/
		uint32_t getPlayPosition();

		/**	@brief	Set time scale
		*/
		void setClockRate(uint32_t tempo, uint32_t samplerate);

	private:
		uint8_t m_nChannel = 0; // MIDI channel
		uint8_t m_nOutput = 0; // JACK output
		uint8_t m_nState = STOP; // Play state
		uint32_t m_nPosition = 0; // Play position in clock cycles
		uint32_t m_nDivisor = 1; // Current pattern clock divisor
		uint32_t m_nDivCount = 0; // Current count of clock cycles within divisor
		std::map<uint32_t,Pattern*> m_mPatterns; // Map of patterns, indexed by start position
		int m_nCurrentPattern = -1; // Start position of pattern currently being played
		int m_nNextEvent = -1; // Index of next event to process or -1 if no more events at this clock cycle
		int8_t m_nEventValue; // Value of event at current interpolation point or -1 if no event
		uint32_t m_nCurrentTime = 0; // Time of last clock pulse (sample)
		uint32_t m_nPatternCursor = 0; // Postion within pattern (step)
		uint32_t m_nSequenceLength = 0; // Quantity of clock cycles in sequence (last pattern start + length)
		uint32_t m_nSamplePerClock; // Quantity of samples per MIDI clock cycle used to schedule future events, e.g. note off / interpolation
		uint32_t m_nSamplerate = 44100; // Samplerate of JACK server
};
