# Technyx

Technyx is a collection of modding tools and research for racing titles made by Eutechnyx.

This toolset runs on Spike foundation.

Head to this **[Wiki](https://github.com/PredatorCZ/Spike/wiki/Spike)** for more information on how to effectively use it.
<h2>Module list</h2>
<ul>
<li><a href="#Arc-Animations">Arc Animations</a></li>
<li><a href="#Arc-Extract">Arc Extract</a></li>
<li><a href="#Extract-CDFILES">Extract CDFILES</a></li>
<li><a href="#LDA-to-text">LDA to text</a></li>
</ul>

## Arc Animations

### Module command: arc_anim

Extracts animations from Arcbanks onto gltf model.

Suported titles: Street Racing Syndicate (PC) only.

> [!NOTE]
> The following file patterns apply to `batch.json` which is described [HERE](https://github.com/PredatorCZ/Spike/wiki/Spike---Batching)

### Main file patterns: `.glb$`, `.gltf$`

### Secondary file patterns: `.ARC$`

## Arc Extract

### Module command: arc_extract

Converts Arcbank into gltf model. Unlinked assets, that are not port of gltf model will be extracted separately.

Suported titles:

|Title|PC|
|---|---|
|Big Mutha Truckers 2|✔|
|Street Racing Syndicate|✔|

### Input file patterns: `.ARC$`

## Extract CDFILES

### Module command: cdfiles_extract

Extracts `CDFILES.DAT`/`ARCHIVE.AR` pairs.

Suported titles:

|Title|PC|PS2|XBOX|GC|PS3|X360|WII|
|---|---|---|---|---|---|---|---|
|Absolute Supercars|||||✔|||
|Big Mutha Truckers||✔|✔|✔||||
|Big Mutha Truckers 2|✔|✔|✔|||||
|Ferrari Challange||✔|||✔||✔|
|Ferrari: The Race|||||✔||✔|
|Ford Mustang||✔|✔|||||
|Ford vs. Chevy||✔|✔|||||
|Hot Wheels: Beat That!|✔||✔|||✔|✔|
|Hummer Badlands||✔|✔|||||
|NASCAR The Game 2011|||||✔|||
|NASCAR Inside Line|||||✔|||
|NASCAR The Game 2013|✔|||||||
|NASCAR '14 and '15|✔||||✔|||
|Pimp My Ride||✔||||✔||
|Supercar Challange|||||✔|||
|Cartoon Network Racing||✔||||||
|The Fast and the Furious||✔||||||
|Street Racing Syndicate|✔|✔|✔|✔||||

### Input file patterns: `cdfiles*.dat$`, `CDFILES*.DAT$`, `CDFILES*.dat$`

## LDA to text

### Module command: lda_to_txt

Converts LDA format into text file.

Suported titles: All

### Input file patterns: `.LDA$`

## [Latest Release](https://github.com/PredatorCZ/Technyx/releases)

## License

This toolset is available under GPL v3 license. (See LICENSE)\
This toolset uses following libraries:

- Spike, Copyright (c) 2016-2024 Lukas Cone (Apache 2)
