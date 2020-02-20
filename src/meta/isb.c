#include "meta.h"
#include "../coding/coding.h"


/* .ISB - Creative ISACT (Interactive Spatial Audio Composition Tools) middleware [Psychonauts (PC), Mass Effect (PC/Xbox 360/PS3)] */
VGMSTREAM* init_vgmstream_isb(STREAMFILE* streamFile) {


	VGMSTREAM* vgmstream = NULL;
	off_t start_offset = 0, name_offset = 0;
	size_t stream_size = 0, name_size = 0;
	int loop_flag = 0, channel_count = 0, sample_rate = 0, codec = 0, pcm_bytes = 0, bps = 0;
	int total_subsongs, target_subsong = streamFile->stream_index;

	//Some LE/BE calls seem reversed, I'm not sure why they were originally like this.

	/* checks */
	if (!check_extensions(streamFile, "isb"))
		goto fail;
	long riffmagic = read_32bitBE(0x00, streamFile);
	if (riffmagic != 0x52494646 && riffmagic != 0x46464952)
		goto fail; //Not RIFF header

	bool isLE = riffmagic == 0x52494646; //Little Endian = PC
	if ((isLE ? read_32bitLE(0x04, streamFile) : read_32bitBE(0x04, streamFile)) + 0x08 != get_streamfile_size(streamFile))
		goto fail;
	if ((isLE ? read_32bitBE(0x08, streamFile) : read_32bitLE(0x08, streamFile)) != 0x69736266) /* "isbf" */
		goto fail;

	/* some files have a companion .icb, seems to be a cue file pointing here */

	/* format is RIFF with many custom chunks, apparently for their DAW-like editor with
	 * complex functions, but most seem always included by default and unused, and games
	 * Psychonauts seems to use the format as a simple audio bank. Mass Effect (X360)
	 * apparently uses ISACT, while Psychonauts Xbox/PS2 don't. */


	off_t offset, max_offset, header_offset = 0;
	size_t header_size = 0;

	total_subsongs = 0; /* not specified */
	if (target_subsong == 0) target_subsong = 1;

	/* parse base RIFF */
	offset = 0x0c;
	max_offset = get_streamfile_size(streamFile);
	while (offset < max_offset) {
		uint32_t chunk_type = isLE ? read_u32be(offset + 0x00, streamFile) : read_u32le(offset + 0x00, streamFile);
		uint32_t chunk_size = isLE ? read_s32le(offset + 0x04, streamFile) : read_s32be(offset + 0x04, streamFile);
		offset += 0x08;

		switch (chunk_type) {
		case 0x4C495354: /* "LIST" */
			if ((isLE ? read_u32be(offset, streamFile) : read_u32le(offset, streamFile)) != 0x73616D70) /* "samp" */
				break; /* there are "bfob" LIST without data */

			total_subsongs++;
			if (target_subsong == total_subsongs && header_offset == 0) {
				header_offset = offset;
				header_size = chunk_size;
			}
			break;

		default: /* most are common chunks at the start that seem to contain defaults */
			break;
		}

		//if (offset + chunk_size+0x01 <= max_offset && chunk_size % 0x02)
		//    chunk_size += 0x01;
		offset += chunk_size;
	}

	if (target_subsong < 0 || target_subsong > total_subsongs || total_subsongs < 1) goto fail;
	if (header_offset == 0) goto fail;
	/* parse header inside LIST */
	offset = header_offset + 0x04;
	max_offset = offset + header_size;
	while (offset < max_offset) {
		uint32_t chunk_type = isLE ? read_u32be(offset + 0x00, streamFile) : read_u32le(offset + 0x00, streamFile);
		uint32_t chunk_size = isLE ? read_s32le(offset + 0x04, streamFile) : read_s32be(offset + 0x04, streamFile);
		offset += 0x08;

		switch (chunk_type) {
		case 0x7469746C: /* "titl" */
			name_offset = offset;
			name_size = chunk_size;
			break;

		case 0x63686E6B: /* "chnk" */
			channel_count = isLE ? read_u32le(offset + 0x00, streamFile) : read_u32be(offset + 0x00, streamFile);
			break;

		case 0x73696E66: /* "sinf" */
			/* 0x00: null? */
			/* 0x04: some value? */
			sample_rate = isLE ? read_u32le(offset + 0x08, streamFile) : read_u32be(offset + 0x08, streamFile);
			pcm_bytes = isLE ? read_u32le(offset + 0x0c, streamFile) : read_u32be(offset + 0x0c, streamFile);
			bps = isLE ? read_u16le(offset + 0x10, streamFile) : read_u16be(offset + 0x10, streamFile);
			/* 0x12: some value? */
			break;

		case 0x636D7069: /* "cmpi" */
			codec = isLE ? read_u32le(offset, streamFile) : read_u32be(offset + 0x00, streamFile);
			if ((isLE ? read_u32le(offset + 0x04, streamFile) : read_u32be(offset + 0x04, streamFile)) != codec) {
				VGM_LOG("ISB: unknown compression repeat\n");
				goto fail;
			}
			/* 0x08: extra value for some codecs? */
			/* 0x0c: block size when codec is XBOX-IMA */
			/* 0x10: null? */
			/* 0x14: flags? */
			break;

		case 0x64617461: /* "data" */
			start_offset = offset;
			stream_size = chunk_size;
			break;

		default: /* most of the same default chunks */
			break;
		}

		//if (offset + chunk_size+0x01 <= max_offset && chunk_size % 0x02)
		//    chunk_size += 0x01;
		offset += chunk_size;
	}

	if (start_offset == 0)
		goto fail;

	/* some files are marked */
	loop_flag = 0;


	/* build the VGMSTREAM */
	vgmstream = allocate_vgmstream(channel_count, loop_flag);
	if (!vgmstream) goto fail;

	vgmstream->meta_type = meta_WAF; //todo
	vgmstream->sample_rate = sample_rate;
	vgmstream->num_streams = total_subsongs;
	vgmstream->stream_size = stream_size;

	switch (codec) {
	case 0x00:
		if (bps == 8) {
			vgmstream->coding_type = coding_PCM8_U;
			vgmstream->layout_type = layout_interleave;
			vgmstream->interleave_block_size = 0x01;
		}
		else {
			vgmstream->coding_type = coding_PCM16LE;
			vgmstream->layout_type = layout_interleave;
			vgmstream->interleave_block_size = 0x02;
		}
		vgmstream->num_samples = pcm_bytes_to_samples(stream_size, channel_count, bps); /* unsure about pcm_bytes */
		break;

	case 0x01:
		vgmstream->coding_type = coding_XBOX_IMA;
		vgmstream->layout_type = layout_none;
		vgmstream->num_samples = xbox_ima_bytes_to_samples(stream_size, channel_count); /* pcm_bytes has excess data */
		break;

#ifdef VGM_USE_VORBIS
	case 0x02:
		vgmstream->codec_data = init_ogg_vorbis(streamFile, start_offset, stream_size, NULL);
		if (!vgmstream->codec_data) goto fail;
		vgmstream->coding_type = coding_OGG_VORBIS;
		vgmstream->layout_type = layout_none;
		vgmstream->num_samples = pcm_bytes / channel_count / (bps / 8);
		break;
#endif
#ifdef VGM_USE_FFMPEG
	case 0x04: {
		//XMA
		uint8_t buf[0x100];
		size_t bytes;
		off_t fmt_offset = start_offset;
		size_t fmt_size = 0x20;

		start_offset += fmt_size;
		stream_size -= fmt_size;

		/* XMA1 "fmt" chunk (BE, unlike the usual LE) */
		bytes = ffmpeg_make_riff_xma_from_fmt_chunk(buf, sizeof(buf), fmt_offset, fmt_size, stream_size, streamFile, 1);
		vgmstream->codec_data = init_ffmpeg_header_offset(streamFile, buf, bytes, start_offset, stream_size);
		if (!vgmstream->codec_data) goto fail;
		vgmstream->coding_type = coding_FFmpeg;
		vgmstream->layout_type = layout_none;

		vgmstream->num_samples = pcm_bytes / channel_count / (bps / 8);
		xma_fix_raw_samples(vgmstream, streamFile, start_offset, stream_size, fmt_offset, 1, 1);
		break;
	}
#endif
	case 0x05:
		//some sort of sony codec - starts with MSFC - MSF format? Sony ATRAC3 maybe?
		STREAMFILE * temp_sf = setup_subfile_streamfile(streamFile, start_offset, stream_size, "msf");
		if (!temp_sf) goto fail;
		vgmstream = init_vgmstream_msf(temp_sf);
		if (!vgmstream) goto fail;
		break;
	default: /* according to press releases ISACT may support WMA and XMA */
		VGM_LOG("ISB: unknown codec %i\n", codec);
		goto fail;
	}

	if (name_offset) { /* UTF16 but only uses lower bytes */
		if (name_size > STREAM_NAME_SIZE)
			name_size = STREAM_NAME_SIZE;
		if (isLE) {
			read_string_utf16le(vgmstream->stream_name, name_size, name_offset, streamFile);
		}
		else {
			read_string_utf16be(vgmstream->stream_name, name_size, name_offset, streamFile);
		}
	}

	if (!vgmstream_open_stream(vgmstream, streamFile, start_offset))
		goto fail;
	return vgmstream;

fail:
	close_vgmstream(vgmstream);
	return NULL;
}
