import CoreAudio
import Foundation

struct OutputDevice: Codable {
    let id: UInt32
    let name: String
    let uid: String
    let isOutput: Bool
    let sampleRates: [Double]
    let isDefaultOutput: Bool
    let isDefaultSystemOutput: Bool
}

struct DeviceSelectorResult: Codable {
    let command: String
    let uid: String
    let originalDefaultOutputID: UInt32
    let originalDefaultSystemOutputID: UInt32
    let originalDefaultOutputUID: String
    let originalDefaultSystemOutputUID: String
    let selectedDeviceID: UInt32
    let readbackDefaultOutputID: UInt32
    let readbackDefaultSystemOutputID: UInt32
}

struct VolumeControl: Codable {
    let element: UInt32
    let value: Float32?
    let set: Bool
    let error: String?
}

struct VolumeResult: Codable {
    let command: String
    let uid: String
    let deviceID: UInt32
    let requested: Float32
    let controls: [VolumeControl]
}

enum ToolError: Error, CustomStringConvertible {
    case message(String)

    var description: String {
        switch self {
        case .message(let text): return text
        }
    }
}

@inline(__always)
func fourCC(_ value: UInt32) -> String {
    let bytes = [UInt8((value >> 24) & 0xff), UInt8((value >> 16) & 0xff), UInt8((value >> 8) & 0xff), UInt8(value & 0xff)]
    return String(bytes: bytes, encoding: .ascii) ?? String(value)
}

func propertyAddress(_ selector: AudioObjectPropertySelector,
                     scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
                     element: AudioObjectPropertyElement = kAudioObjectPropertyElementMain) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: element)
}

func propertySize(_ object: AudioObjectID, _ address: AudioObjectPropertyAddress) throws -> UInt32 {
    var size: UInt32 = 0
    var mutableAddress = address
    let status = AudioObjectGetPropertyDataSize(object, &mutableAddress, 0, nil, &size)
    guard status == noErr else { throw ToolError.message("CoreAudio property size failed: \(fourCC(UInt32(bitPattern: status)))") }
    return size
}

func propertyData<T>(_ object: AudioObjectID, _ address: AudioObjectPropertyAddress, as type: T.Type) throws -> T {
    return try withUnsafeTemporaryAllocation(of: T.self, capacity: 1) { buffer in
        var size = UInt32(MemoryLayout<T>.size)
        var mutableAddress = address
        let status = AudioObjectGetPropertyData(object, &mutableAddress, 0, nil, &size, buffer.baseAddress!)
        guard status == noErr else { throw ToolError.message("CoreAudio property read failed: \(fourCC(UInt32(bitPattern: status)))") }
        return buffer.baseAddress!.pointee
    }
}

func stringProperty(_ object: AudioObjectID, _ address: AudioObjectPropertyAddress) throws -> String {
    let value: CFString = try propertyData(object, address, as: CFString.self)
    return value as String
}

func deviceIDs() throws -> [AudioObjectID] {
    let address = propertyAddress(kAudioHardwarePropertyDevices)
    let size = try propertySize(AudioObjectID(kAudioObjectSystemObject), address)
    var ids = Array(repeating: AudioObjectID(0), count: Int(size) / MemoryLayout<AudioObjectID>.size)
    var mutableAddress = address
    var mutableSize = size
    let status = ids.withUnsafeMutableBytes { bytes in
        AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &mutableAddress, 0, nil, &mutableSize, bytes.baseAddress!)
    }
    guard status == noErr else { throw ToolError.message("CoreAudio device list failed: \(fourCC(UInt32(bitPattern: status)))") }
    return ids
}

func hasOutput(_ id: AudioObjectID) -> Bool {
    do {
        let address = propertyAddress(kAudioDevicePropertyStreamConfiguration, scope: kAudioObjectPropertyScopeOutput)
        let size = try propertySize(id, address)
        guard size >= UInt32(MemoryLayout<AudioBufferList>.size) else { return false }
        var bytes = [UInt8](repeating: 0, count: Int(size))
        var mutableAddress = address
        var mutableSize = size
        let status = bytes.withUnsafeMutableBytes { rawBytes in
            AudioObjectGetPropertyData(id, &mutableAddress, 0, nil, &mutableSize,
                                       rawBytes.baseAddress!.assumingMemoryBound(to: AudioBufferList.self))
        }
        guard status == noErr else { return false }
        return bytes.withUnsafeMutableBytes { rawBytes in
            let list = UnsafeMutableAudioBufferListPointer(
                rawBytes.baseAddress!.assumingMemoryBound(to: AudioBufferList.self))
            return list.reduce(0) { $0 + Int($1.mNumberChannels) } > 0
        }
    } catch {
        return false
    }
}

func sampleRates(_ id: AudioObjectID) -> [Double] {
    do {
        let address = propertyAddress(kAudioDevicePropertyAvailableNominalSampleRates, scope: kAudioObjectPropertyScopeGlobal)
        let size = try propertySize(id, address)
        var ranges = Array(repeating: AudioValueRange(mMinimum: 0, mMaximum: 0), count: Int(size) / MemoryLayout<AudioValueRange>.size)
        var mutableAddress = address
        var mutableSize = size
        let status = ranges.withUnsafeMutableBytes { bytes in
            AudioObjectGetPropertyData(id, &mutableAddress, 0, nil, &mutableSize, bytes.baseAddress!)
        }
        guard status == noErr else { return [] }
        return ranges.flatMap { range in
            range.mMinimum == range.mMaximum ? [range.mMinimum] : [range.mMinimum, range.mMaximum]
        }
    } catch {
        return []
    }
}

func defaultDevice(_ selector: AudioObjectPropertySelector) throws -> AudioObjectID {
    try propertyData(AudioObjectID(kAudioObjectSystemObject), propertyAddress(selector), as: AudioObjectID.self)
}

func outputDevices() throws -> [OutputDevice] {
    let defaultOutput = try defaultDevice(kAudioHardwarePropertyDefaultOutputDevice)
    let defaultSystem = try defaultDevice(kAudioHardwarePropertyDefaultSystemOutputDevice)
    return try deviceIDs().filter(hasOutput).map { id in
        OutputDevice(id: id,
                     name: try stringProperty(id, propertyAddress(kAudioObjectPropertyName)),
                     uid: try stringProperty(id, propertyAddress(kAudioDevicePropertyDeviceUID)),
                     isOutput: true,
                     sampleRates: sampleRates(id),
                     isDefaultOutput: id == defaultOutput,
                     isDefaultSystemOutput: id == defaultSystem)
    }
}

func findOutput(_ uid: String) throws -> (AudioObjectID, OutputDevice) {
    guard let device = try outputDevices().first(where: { $0.uid == uid }) else {
        throw ToolError.message("Output device UID not found: \(uid)")
    }
    return (device.id, device)
}

func setDefault(_ selector: AudioObjectPropertySelector, deviceID: AudioObjectID) throws {
    var id = deviceID
    var address = propertyAddress(selector)
    let status = withUnsafePointer(to: &id) { pointer in
        AudioObjectSetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, UInt32(MemoryLayout<AudioObjectID>.size), pointer)
    }
    guard status == noErr else { throw ToolError.message("CoreAudio default output update failed: \(fourCC(UInt32(bitPattern: status)))") }
}

func selectOutput(_ uid: String) throws -> DeviceSelectorResult {
    let (id, _) = try findOutput(uid)
    let originalOutput = try defaultDevice(kAudioHardwarePropertyDefaultOutputDevice)
    let originalSystem = try defaultDevice(kAudioHardwarePropertyDefaultSystemOutputDevice)
    let originalOutputUID = try stringProperty(originalOutput, propertyAddress(kAudioDevicePropertyDeviceUID))
    let originalSystemUID = try stringProperty(originalSystem, propertyAddress(kAudioDevicePropertyDeviceUID))
    try setDefault(kAudioHardwarePropertyDefaultOutputDevice, deviceID: id)
    do {
        try setDefault(kAudioHardwarePropertyDefaultSystemOutputDevice, deviceID: id)
    } catch {
        // Keep the two defaults consistent if the second CoreAudio write fails.
        try? setDefault(kAudioHardwarePropertyDefaultOutputDevice, deviceID: originalOutput)
        try? setDefault(kAudioHardwarePropertyDefaultSystemOutputDevice, deviceID: originalSystem)
        throw error
    }
    let readbackOutput: AudioObjectID
    let readbackSystem: AudioObjectID
    do {
        readbackOutput = try defaultDevice(kAudioHardwarePropertyDefaultOutputDevice)
        readbackSystem = try defaultDevice(kAudioHardwarePropertyDefaultSystemOutputDevice)
    } catch {
        try? setDefault(kAudioHardwarePropertyDefaultOutputDevice, deviceID: originalOutput)
        try? setDefault(kAudioHardwarePropertyDefaultSystemOutputDevice, deviceID: originalSystem)
        throw error
    }
    guard readbackOutput == id, readbackSystem == id else {
        try? setDefault(kAudioHardwarePropertyDefaultOutputDevice, deviceID: originalOutput)
        try? setDefault(kAudioHardwarePropertyDefaultSystemOutputDevice, deviceID: originalSystem)
        throw ToolError.message("CoreAudio default output readback mismatch; original defaults were restored")
    }
    return DeviceSelectorResult(command: "select", uid: uid,
                                originalDefaultOutputID: originalOutput,
                                originalDefaultSystemOutputID: originalSystem,
                                originalDefaultOutputUID: originalOutputUID,
                                originalDefaultSystemOutputUID: originalSystemUID,
                                selectedDeviceID: id,
                                readbackDefaultOutputID: readbackOutput,
                                readbackDefaultSystemOutputID: readbackSystem)
}

func readScalar(_ id: AudioObjectID, element: AudioObjectPropertyElement) throws -> Float32 {
    try propertyData(id, propertyAddress(kAudioDevicePropertyVolumeScalar, scope: kAudioObjectPropertyScopeOutput, element: element), as: Float32.self)
}

func setScalar(_ id: AudioObjectID, element: AudioObjectPropertyElement, value: Float32) throws {
    var scalar = value
    var address = propertyAddress(kAudioDevicePropertyVolumeScalar, scope: kAudioObjectPropertyScopeOutput, element: element)
    let status = withUnsafePointer(to: &scalar) { pointer in
        AudioObjectSetPropertyData(id, &address, 0, nil, UInt32(MemoryLayout<Float32>.size), pointer)
    }
    guard status == noErr else { throw ToolError.message(fourCC(UInt32(bitPattern: status))) }
}

func volume(_ uid: String, _ requested: Float32) throws -> VolumeResult {
    let (id, _) = try findOutput(uid)
    var controls: [VolumeControl] = []
    for element: AudioObjectPropertyElement in 0...2 {
        var address = propertyAddress(kAudioDevicePropertyVolumeScalar, scope: kAudioObjectPropertyScopeOutput, element: element)
        guard AudioObjectHasProperty(id, &address) else { continue }
        var settable = DarwinBoolean(false)
        guard AudioObjectIsPropertySettable(id, &address, &settable) == noErr, settable.boolValue else { continue }
        do {
            try setScalar(id, element: element, value: requested)
            controls.append(VolumeControl(element: element, value: try readScalar(id, element: element), set: true, error: nil))
        } catch {
            controls.append(VolumeControl(element: element, value: try? readScalar(id, element: element), set: false, error: String(describing: error)))
        }
    }
    guard controls.contains(where: { $0.set }) else {
        throw ToolError.message("No writable output volume control for UID: \(uid)")
    }
    return VolumeResult(command: "volume", uid: uid, deviceID: id, requested: requested, controls: controls)
}

func printJSON<T: Encodable>(_ value: T) throws {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    FileHandle.standardOutput.write(try encoder.encode(value))
    FileHandle.standardOutput.write(Data("\n".utf8))
}

let arguments = CommandLine.arguments
do {
    guard arguments.count >= 2 else { throw ToolError.message("usage: mac_audio_output.swift list | select <UID> | volume <UID> <0..1>") }
    switch arguments[1] {
    case "list":
        try printJSON(outputDevices())
    case "select":
        guard arguments.count == 3 else { throw ToolError.message("usage: select <UID>") }
        try printJSON(selectOutput(arguments[2]))
    case "volume":
        guard arguments.count == 4, let value = Float32(arguments[3]), (0...1).contains(value) else {
            throw ToolError.message("usage: volume <UID> <0..1>")
        }
        try printJSON(volume(arguments[2], value))
    default:
        throw ToolError.message("unknown command: \(arguments[1])")
    }
} catch {
    FileHandle.standardError.write(Data("error: \(error)\n".utf8))
    exit(1)
}
