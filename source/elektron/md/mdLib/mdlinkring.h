#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp56kEmu/essi.h"

namespace md
{
	// Inter-DSP ESSI0 link mailbox: frames one DSP transmits, waiting for the peer
	// DSP's receive callback. It replaces the peer ESSI's own Audio input ring for
	// this purpose with the same FIFO order and the same capacity limit, and holds
	// only the one word per slot the link carries.
	//
	// The Audio ring keeps 32768 frames of 516 bytes (~17 MB) and walks through all
	// of them even while the link is nearly empty, so every word written and read
	// touched cold cache lines. This queue restarts at its first entry whenever it
	// drains, which the link does continuously, so the touched memory follows the
	// actual depth. Storage is allocated once at construction; nothing allocates
	// while emulating. Single-threaded: only the scheduler thread uses it.
	class LinkInputRing
	{
	public:
		using DspRing = std::remove_reference_t<decltype(std::declval<dsp56k::Essi&>().getAudioInputs())>;
		static constexpr size_t Capacity = DspRing::capacity();

		LinkInputRing() : m_entries(Capacity) {}

		bool empty() const { return m_size == 0; }
		bool full() const { return m_size >= Capacity; }
		size_t size() const { return m_size; }

		// The link word of each slot, as the ESSI0 wire carries it (TX register 0).
		void push_back(const dsp56k::Audio::TxFrame& _tx)
		{
			auto& e = pushEntry();
			e.count = _tx.size();
			for(uint32_t i = 0; i < e.count; ++i)
				e.words[i] = _tx[i][0];
		}

		// A received frame whose slots carry one word each (RX register 0).
		void push_back(const dsp56k::Audio::RxFrame& _rx)
		{
			auto& e = pushEntry();
			e.count = _rx.size();
			for(uint32_t i = 0; i < e.count; ++i)
				e.words[i] = _rx[i][0];
		}

		// Same result as assigning the Audio ring's front frame: the slot count and
		// the first count slots are replaced, later slots of _dst are left as they are.
		void pop_front(dsp56k::Audio::RxFrame& _dst)
		{
			const auto& e = m_entries[m_head];
			_dst.resize(e.count);
			for(uint32_t i = 0; i < e.count; ++i)
				_dst[i] = dsp56k::Audio::RxSlot{e.words[i]};
			popEntry();
		}

		void pop_front()
		{
			popEntry();
		}

	private:
		struct Entry
		{
			uint32_t count = 0;
			std::array<dsp56k::TWord, dsp56k::Audio::MaxSlotsPerFrame> words{};
		};

		Entry& pushEntry()
		{
			auto& e = m_entries[m_tail];
			m_tail = m_tail + 1 == Capacity ? 0 : m_tail + 1;
			++m_size;
			return e;
		}

		void popEntry()
		{
			if(--m_size == 0)
			{
				m_head = m_tail = 0;	// drained: continue at the (cache-hot) first entry
				return;
			}
			m_head = m_head + 1 == Capacity ? 0 : m_head + 1;
		}

		std::vector<Entry> m_entries;
		size_t m_head = 0;
		size_t m_tail = 0;
		size_t m_size = 0;
	};
}
