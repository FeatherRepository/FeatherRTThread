# Bridges the current Windows render endpoint (including RDP Remote Audio)
# into the FeatherTalk UAC2 WaveRT render pin.
param(
    [string]$DevicePath = '',
    [double]$Seconds = 0,
    [int]$LogIntervalSeconds = 2,
    [uint32]$TargetSampleRate = 48000,
    [ValidateSet(16, 24)] [int]$TargetBits = 16,
    [ValidateSet(1, 2)] [int]$TargetChannels = 2,
    [switch]$FollowDeviceFormat,
    [switch]$Reconnect,
    [string]$ControlPort = 'COM17'
)

$source = @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
class BridgeMMDeviceEnumeratorComObject { }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IBridgeMMDeviceEnumerator
{
    int EnumAudioEndpoints(int dataFlow, uint stateMask, out IntPtr devices);
    int GetDefaultAudioEndpoint(int dataFlow, int role, out IBridgeMMDevice device);
    int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IBridgeMMDevice device);
    int RegisterEndpointNotificationCallback(IntPtr client);
    int UnregisterEndpointNotificationCallback(IntPtr client);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IBridgeMMDevice
{
    int Activate(ref Guid iid, uint clsctx, IntPtr activationParams,
        [MarshalAs(UnmanagedType.IUnknown)] out object instance);
    int OpenPropertyStore(uint access, out IntPtr properties);
    int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    int GetState(out uint state);
}

[ComImport, Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IBridgeAudioClient
{
    int Initialize(int shareMode, uint streamFlags, long bufferDuration,
        long periodicity, IntPtr format, IntPtr sessionGuid);
    int GetBufferSize(out uint frames);
    int GetStreamLatency(out long latency);
    int GetCurrentPadding(out uint frames);
    int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closestMatch);
    int GetMixFormat(out IntPtr format);
    int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);
    int Start();
    int Stop();
    int Reset();
    int SetEventHandle(IntPtr eventHandle);
    int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
}

[ComImport, Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IBridgeAudioCaptureClient
{
    int GetBuffer(out IntPtr data, out uint frames, out uint flags,
        out ulong devicePosition, out ulong qpcPosition);
    int ReleaseBuffer(uint frames);
    int GetNextPacketSize(out uint frames);
}

public static class FeatherDefaultAudioBridge
{
    const uint GENERIC_READ = 0x80000000;
    const uint GENERIC_WRITE = 0x40000000;
    const uint FILE_SHARE_READ = 1;
    const uint FILE_SHARE_WRITE = 2;
    const uint OPEN_EXISTING = 3;
    const uint IOCTL_KS_PROPERTY = 0x002F0003;
    const uint KSPROPERTY_TYPE_GET = 1;
    const uint KSPROPERTY_TYPE_SET = 2;
    const uint AUDCLNT_STREAMFLAGS_LOOPBACK = 0x00020000;
    const uint AUDCLNT_BUFFERFLAGS_SILENT = 0x00000002;
    const uint WAIT_OBJECT_0 = 0;
    const uint WAIT_TIMEOUT = 258;
    const uint NOTIFICATION_COUNT = 2;
    const uint DIGCF_PRESENT = 0x00000002;
    const uint DIGCF_DEVICEINTERFACE = 0x00000010;

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA
    {
        public uint cbSize;
        public Guid InterfaceClassGuid;
        public uint Flags;
        public UIntPtr Reserved;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateEvent(IntPtr securityAttributes, bool manualReset,
        bool initialState, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("ksuser.dll")]
    static extern int KsCreatePin(SafeFileHandle filterHandle, IntPtr connect,
        uint desiredAccess, out SafeFileHandle connectionHandle);

    [DllImport("ksproxy.ax")]
    static extern int KsSynchronousDeviceControl(SafeFileHandle device, uint code,
        IntPtr input, uint inputSize, IntPtr output, uint outputSize, ref uint returned);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, string enumerator,
        IntPtr parent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr deviceInfo,
        ref Guid classGuid, uint index, ref SP_DEVICE_INTERFACE_DATA data);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set,
        ref SP_DEVICE_INTERFACE_DATA data, IntPtr detail, uint detailSize,
        out uint requiredSize, IntPtr deviceInfo);

    [DllImport("setupapi.dll")]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    sealed class SourceFormat
    {
        public ushort Tag;
        public ushort Channels;
        public uint Rate;
        public ushort BlockAlign;
        public ushort Bits;
        public ushort ValidBits;
        public bool IsFloat;
        public string Description;
    }

    sealed class PcmRing
    {
        readonly byte[] data;
        readonly int bits;
        readonly int channels;
        int read;
        int write;
        int count;
        public long DroppedBytes;
        public long UnderflowBytes;
        public long ProducedFrames;

        public PcmRing(uint rate, int targetBits, int targetChannels)
        {
            bits = targetBits;
            channels = targetChannels;
            int frameBytes = (bits / 8) * channels;
            data = new byte[checked((int)rate * frameBytes * 2)];
        }

        void Put(byte value)
        {
            if (count == data.Length)
            {
                read = (read + 1) % data.Length;
                count--;
                DroppedBytes++;
            }
            data[write] = value;
            write = (write + 1) % data.Length;
            count++;
        }

        static double Clip(double value)
        {
            if (value > 1.0) return 1.0;
            if (value < -1.0) return -1.0;
            return value;
        }

        void PutSample(double value)
        {
            value = Clip(value);
            if (bits == 16)
            {
                short sample = (short)Math.Round(value *
                    (value >= 0 ? 32767.0 : 32768.0));
                Put((byte)sample); Put((byte)(sample >> 8));
            }
            else
            {
                int sample = (int)Math.Round(value *
                    (value >= 0 ? 8388607.0 : 8388608.0));
                Put((byte)sample); Put((byte)(sample >> 8));
                Put((byte)(sample >> 16));
            }
        }

        public void PutFrame(double left, double right)
        {
            if (channels == 1) PutSample((left + right) * 0.5);
            else { PutSample(left); PutSample(right); }
            ProducedFrames++;
        }

        public int AvailableBytes { get { return count; } }

        public void ReadTo(IntPtr destination, int bytes)
        {
            byte[] output = new byte[bytes];
            int copied = 0;
            while (copied < bytes && count > 0)
            {
                output[copied++] = data[read];
                read = (read + 1) % data.Length;
                count--;
            }
            if (copied < bytes) UnderflowBytes += bytes - copied;
            Marshal.Copy(output, 0, destination, bytes);
        }
    }

    sealed class StereoResampler
    {
        readonly double step;
        readonly PcmRing output;
        bool havePrevious;
        float previousLeft;
        float previousRight;
        long inputIndex = -1;
        double nextOutputPosition;
        double peak;
        double squareSum;
        long sampleCount;

        public double Peak { get { return peak; } }
        public double Rms
        {
            get { return sampleCount == 0 ? 0.0 : Math.Sqrt(squareSum / sampleCount); }
        }

        public StereoResampler(uint sourceRate, uint targetRate, PcmRing ring)
        {
            step = (double)sourceRate / targetRate;
            output = ring;
        }

        public void Add(float left, float right)
        {
            inputIndex++;
            if (!havePrevious)
            {
                previousLeft = left;
                previousRight = right;
                havePrevious = true;
                return;
            }
            double leftIndex = inputIndex - 1;
            while (nextOutputPosition <= inputIndex)
            {
                double fraction = nextOutputPosition - leftIndex;
                if (fraction < 0.0) fraction = 0.0;
                if (fraction > 1.0) fraction = 1.0;
                double l = previousLeft + (left - previousLeft) * fraction;
                double r = previousRight + (right - previousRight) * fraction;
                double absLeft = Math.Abs(l);
                double absRight = Math.Abs(r);
                if (absLeft > peak) peak = absLeft;
                if (absRight > peak) peak = absRight;
                squareSum += l * l + r * r;
                sampleCount += 2;
                output.PutFrame(l, r);
                nextOutputPosition += step;
            }
            previousLeft = left;
            previousRight = right;
        }
    }

    static void Check(int hr, string operation)
    {
        if (hr < 0)
            throw new COMException(operation + " failed: 0x" +
                unchecked((uint)hr).ToString("X8"), hr);
    }

    static string FindFeatherWaveRtPath()
    {
        Guid category = new Guid("6994AD04-93EF-11D0-A3CC-00A0C9223196");
        IntPtr set = SetupDiGetClassDevs(ref category, null, IntPtr.Zero,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == new IntPtr(-1))
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                "SetupDiGetClassDevs(KSCATEGORY_AUDIO)");
        try
        {
            for (uint index = 0; ; ++index)
            {
                SP_DEVICE_INTERFACE_DATA data = new SP_DEVICE_INTERFACE_DATA();
                data.cbSize = checked((uint)Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA)));
                if (!SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref category,
                    index, ref data))
                {
                    int error = Marshal.GetLastWin32Error();
                    if (error == 259) break;
                    throw new Win32Exception(error, "SetupDiEnumDeviceInterfaces");
                }
                uint required;
                SetupDiGetDeviceInterfaceDetail(set, ref data, IntPtr.Zero, 0,
                    out required, IntPtr.Zero);
                IntPtr detail = Marshal.AllocHGlobal(checked((int)required));
                try
                {
                    Marshal.WriteInt32(detail, IntPtr.Size == 8 ? 8 : 6);
                    if (!SetupDiGetDeviceInterfaceDetail(set, ref data, detail,
                        required, out required, IntPtr.Zero))
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "SetupDiGetDeviceInterfaceDetail");
                    string candidate = Marshal.PtrToStringUni(IntPtr.Add(detail, 4));
                    if (candidate != null &&
                        candidate.IndexOf("vid_ffff&pid_f502", StringComparison.OrdinalIgnoreCase) >= 0 &&
                        candidate.IndexOf("msft_wave", StringComparison.OrdinalIgnoreCase) >= 0)
                        return candidate;
                }
                finally { Marshal.FreeHGlobal(detail); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(set); }
        throw new InvalidOperationException("FeatherTalk UAC WaveRT interface is not present");
    }

    static void GuidAt(byte[] bytes, int offset, string value)
    {
        Buffer.BlockCopy(new Guid(value).ToByteArray(), 0, bytes, offset, 16);
    }

    static void UInt16At(byte[] bytes, int offset, ushort value)
    {
        Buffer.BlockCopy(BitConverter.GetBytes(value), 0, bytes, offset, 2);
    }

    static void UInt32At(byte[] bytes, int offset, uint value)
    {
        Buffer.BlockCopy(BitConverter.GetBytes(value), 0, bytes, offset, 4);
    }

    static byte[] Connection(uint sampleRate, int bits, int channels)
    {
        ushort blockAlign = checked((ushort)((bits / 8) * channels));
        byte[] bytes = new byte[160];
        GuidAt(bytes, 0, "1A8766A0-62CE-11CF-A5D6-28DB04C10000");
        UInt32At(bytes, 16, 1);
        GuidAt(bytes, 24, "4747B320-62CE-11CF-A5D6-28DB04C10000");
        UInt32At(bytes, 40, 0);
        UInt32At(bytes, 48, 1);
        UInt32At(bytes, 64, 0x40000000);
        UInt32At(bytes, 68, 1);
        UInt32At(bytes, 72, 82);
        UInt32At(bytes, 80, 4);
        GuidAt(bytes, 88, "73647561-0000-0010-8000-00AA00389B71");
        GuidAt(bytes, 104, "00000001-0000-0010-8000-00AA00389B71");
        GuidAt(bytes, 120, "05589F81-C356-11CE-BF01-00AA0055595A");
        UInt16At(bytes, 136, 1);
        UInt16At(bytes, 138, 2);
        UInt32At(bytes, 140, sampleRate);
        UInt32At(bytes, 144, sampleRate * blockAlign);
        UInt16At(bytes, 148, blockAlign);
        UInt16At(bytes, 150, checked((ushort)bits));
        UInt16At(bytes, 152, 0);
        return bytes;
    }

    static int KsProperty(SafeFileHandle pin, byte[] descriptor, IntPtr value,
        uint valueSize, out uint returned)
    {
        IntPtr request = Marshal.AllocHGlobal(descriptor.Length);
        try
        {
            Marshal.Copy(descriptor, 0, request, descriptor.Length);
            returned = 0;
            return KsSynchronousDeviceControl(pin, IOCTL_KS_PROPERTY, request,
                (uint)descriptor.Length, value, valueSize, ref returned);
        }
        finally { Marshal.FreeHGlobal(request); }
    }

    static byte[] Property(string set, uint id, uint flags, int size)
    {
        byte[] bytes = new byte[size];
        GuidAt(bytes, 0, set);
        UInt32At(bytes, 16, id);
        UInt32At(bytes, 20, flags);
        return bytes;
    }

    static void SetState(SafeFileHandle pin, uint state)
    {
        byte[] property = Property("1D58C920-AC9B-11CF-A5D6-28DB04C10000", 0,
            KSPROPERTY_TYPE_SET, 24);
        IntPtr value = Marshal.AllocHGlobal(4);
        try
        {
            Marshal.WriteInt32(value, unchecked((int)state));
            uint returned;
            Check(KsProperty(pin, property, value, 4, out returned),
                "SetState(" + state + ")");
        }
        finally { Marshal.FreeHGlobal(value); }
    }

    static IntPtr AllocateBuffer(SafeFileHandle pin, uint requestedBytes,
        out uint actualBytes)
    {
        byte[] property = Property("A855A48C-2F78-4729-9051-1968746B9EEF", 5,
            KSPROPERTY_TYPE_GET, 40);
        UInt32At(property, 32, requestedBytes);
        UInt32At(property, 36, NOTIFICATION_COUNT);
        IntPtr value = Marshal.AllocHGlobal(16);
        try
        {
            for (int i = 0; i < 16; ++i) Marshal.WriteByte(value, i, 0);
            uint returned;
            Check(KsProperty(pin, property, value, 16, out returned),
                "KSPROPERTY_RTAUDIO_BUFFER_WITH_NOTIFICATION");
            IntPtr address = Marshal.ReadIntPtr(value, 0);
            actualBytes = unchecked((uint)Marshal.ReadInt32(value, 8));
            if (address == IntPtr.Zero || actualBytes == 0)
                throw new InvalidOperationException("WaveRT returned an empty cyclic buffer");
            return address;
        }
        finally { Marshal.FreeHGlobal(value); }
    }

    static void RegisterEvent(SafeFileHandle pin, IntPtr eventHandle)
    {
        byte[] property = Property("A855A48C-2F78-4729-9051-1968746B9EEF", 6,
            KSPROPERTY_TYPE_SET, 32);
        Buffer.BlockCopy(BitConverter.GetBytes(eventHandle.ToInt64()), 0, property, 24, 8);
        uint returned;
        Check(KsProperty(pin, property, IntPtr.Zero, 0, out returned),
            "KSPROPERTY_RTAUDIO_REGISTER_NOTIFICATION_EVENT");
    }

    static void SetWritePacket(SafeFileHandle pin, uint packetNumber)
    {
        byte[] property = Property("A855A48C-2F78-4729-9051-1968746B9EEF", 12,
            KSPROPERTY_TYPE_SET, 24);
        IntPtr value = Marshal.AllocHGlobal(12);
        try
        {
            Marshal.WriteInt32(value, 0, unchecked((int)packetNumber));
            Marshal.WriteInt32(value, 4, 0);
            Marshal.WriteInt32(value, 8, 0);
            uint returned;
            Check(KsProperty(pin, property, value, 12, out returned),
                "KSPROPERTY_RTAUDIO_SETWRITEPACKET(" + packetNumber + ")");
        }
        finally { Marshal.FreeHGlobal(value); }
    }

    static uint GetPacketCount(SafeFileHandle pin)
    {
        byte[] property = Property("A855A48C-2F78-4729-9051-1968746B9EEF", 9,
            KSPROPERTY_TYPE_GET, 24);
        IntPtr value = Marshal.AllocHGlobal(4);
        try
        {
            uint returned;
            Check(KsProperty(pin, property, value, 4, out returned),
                "KSPROPERTY_RTAUDIO_PACKETCOUNT");
            return unchecked((uint)Marshal.ReadInt32(value));
        }
        finally { Marshal.FreeHGlobal(value); }
    }

    static SourceFormat ParseFormat(IntPtr format)
    {
        SourceFormat result = new SourceFormat();
        result.Tag = unchecked((ushort)Marshal.ReadInt16(format, 0));
        result.Channels = unchecked((ushort)Marshal.ReadInt16(format, 2));
        result.Rate = unchecked((uint)Marshal.ReadInt32(format, 4));
        result.BlockAlign = unchecked((ushort)Marshal.ReadInt16(format, 12));
        result.Bits = unchecked((ushort)Marshal.ReadInt16(format, 14));
        result.ValidBits = result.Bits;
        Guid subtype = Guid.Empty;
        if (result.Tag == 0xFFFE && unchecked((ushort)Marshal.ReadInt16(format, 16)) >= 22)
        {
            result.ValidBits = unchecked((ushort)Marshal.ReadInt16(format, 18));
            byte[] guid = new byte[16];
            Marshal.Copy(IntPtr.Add(format, 24), guid, 0, 16);
            subtype = new Guid(guid);
            result.IsFloat = subtype == new Guid("00000003-0000-0010-8000-00AA00389B71");
        }
        else result.IsFloat = result.Tag == 3;
        if (result.Channels == 0 || result.Rate == 0 || result.BlockAlign == 0)
            throw new NotSupportedException("Default audio endpoint returned an invalid format");
        if (!result.IsFloat && result.Tag != 1 && result.Tag != 0xFFFE)
            throw new NotSupportedException("Unsupported source wave format tag 0x" +
                result.Tag.ToString("X4"));
        result.Description = "tag=0x" + result.Tag.ToString("X4") + " " +
            result.Rate + "Hz " + result.Bits + "bit " + result.Channels + "ch" +
            (result.IsFloat ? " float" : " PCM");
        return result;
    }

    static float ReadSample(byte[] bytes, int offset, SourceFormat format)
    {
        if (format.IsFloat && format.Bits == 32)
            return BitConverter.ToSingle(bytes, offset);
        if (format.IsFloat && format.Bits == 64)
            return (float)BitConverter.ToDouble(bytes, offset);
        if (format.Bits == 16)
            return BitConverter.ToInt16(bytes, offset) / 32768.0f;
        if (format.Bits == 24)
        {
            int value = bytes[offset] | (bytes[offset + 1] << 8) |
                (bytes[offset + 2] << 16);
            if ((value & 0x800000) != 0) value |= unchecked((int)0xFF000000);
            return value / 8388608.0f;
        }
        if (format.Bits == 32)
            return (float)(BitConverter.ToInt32(bytes, offset) / 2147483648.0);
        throw new NotSupportedException("Unsupported source sample width " + format.Bits);
    }

    static long DrainCapture(IBridgeAudioCaptureClient capture, SourceFormat format,
        StereoResampler resampler)
    {
        long sourceFrames = 0;
        uint packetFrames;
        Check(capture.GetNextPacketSize(out packetFrames), "GetNextPacketSize");
        while (packetFrames > 0)
        {
            IntPtr pointer;
            uint frames;
            uint flags;
            ulong devicePosition;
            ulong qpcPosition;
            Check(capture.GetBuffer(out pointer, out frames, out flags,
                out devicePosition, out qpcPosition), "IAudioCaptureClient.GetBuffer");
            try
            {
                int bytes = checked((int)(frames * format.BlockAlign));
                byte[] block = new byte[bytes];
                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ||
                    pointer == IntPtr.Zero;
                if (!silent) Marshal.Copy(pointer, block, 0, bytes);
                int sampleBytes = format.Bits / 8;
                for (uint frame = 0; frame < frames; ++frame)
                {
                    float left = 0.0f;
                    float right = 0.0f;
                    if (!silent)
                    {
                        int baseOffset = checked((int)frame * format.BlockAlign);
                        left = ReadSample(block, baseOffset, format);
                        right = format.Channels > 1 ?
                            ReadSample(block, baseOffset + sampleBytes, format) : left;
                    }
                    resampler.Add(left, right);
                }
                sourceFrames += frames;
            }
            finally { Check(capture.ReleaseBuffer(frames), "ReleaseBuffer"); }
            Check(capture.GetNextPacketSize(out packetFrames), "GetNextPacketSize");
        }
        return sourceFrames;
    }

    public static void Run(string path, double seconds, int logIntervalSeconds,
        uint targetRate, int targetBits, int targetChannels)
    {
        if (targetRate != 16000 && targetRate != 24000 &&
            targetRate != 48000 && targetRate != 96000)
            throw new ArgumentOutOfRangeException("targetRate");
        if ((targetBits != 16 && targetBits != 24) ||
            (targetChannels != 1 && targetChannels != 2))
            throw new ArgumentException("Unsupported target sample layout");
        int targetFrameBytes = (targetBits / 8) * targetChannels;
        if (String.IsNullOrEmpty(path)) path = FindFeatherWaveRtPath();
        IBridgeMMDeviceEnumerator enumerator =
            (IBridgeMMDeviceEnumerator)new BridgeMMDeviceEnumeratorComObject();
        IBridgeMMDevice sourceDevice;
        Check(enumerator.GetDefaultAudioEndpoint(0, 1, out sourceDevice),
            "GetDefaultAudioEndpoint(eRender/eMultimedia)");
        string sourceId;
        Check(sourceDevice.GetId(out sourceId), "IMMDevice.GetId");
        Guid audioClientIid = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
        object audioObject;
        Check(sourceDevice.Activate(ref audioClientIid, 23, IntPtr.Zero, out audioObject),
            "Activate(IAudioClient)");
        IBridgeAudioClient audioClient = (IBridgeAudioClient)audioObject;
        IntPtr mixFormat;
        Check(audioClient.GetMixFormat(out mixFormat), "GetMixFormat");
        SourceFormat format = ParseFormat(mixFormat);
        try
        {
            Check(audioClient.Initialize(0, AUDCLNT_STREAMFLAGS_LOOPBACK,
                2000000, 0, mixFormat, IntPtr.Zero), "IAudioClient.Initialize(loopback)");
        }
        finally { Marshal.FreeCoTaskMem(mixFormat); }
        Guid captureIid = new Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317");
        object captureObject;
        Check(audioClient.GetService(ref captureIid, out captureObject),
            "GetService(IAudioCaptureClient)");
        IBridgeAudioCaptureClient capture = (IBridgeAudioCaptureClient)captureObject;

        PcmRing pcm = new PcmRing(targetRate, targetBits, targetChannels);
        StereoResampler resampler = new StereoResampler(format.Rate, targetRate, pcm);
        SafeFileHandle pin = null;
        IntPtr notificationEvent = IntPtr.Zero;
        bool captureStarted = false;
        bool pinRunning = false;
        long sourceFrames = 0;
        long packetsSubmitted = 0;
        try
        {
            using (SafeFileHandle filter = CreateFile(path, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0,
                IntPtr.Zero))
            {
                if (filter.IsInvalid)
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFile(WaveRT)");
                byte[] connection = Connection(targetRate, targetBits, targetChannels);
                IntPtr memory = Marshal.AllocHGlobal(connection.Length);
                try
                {
                    Marshal.Copy(connection, 0, memory, connection.Length);
                    int status = KsCreatePin(filter, memory, GENERIC_WRITE, out pin);
                    if (status != 0 || pin == null || pin.IsInvalid)
                        throw new Win32Exception(status, "KsCreatePin(render pin 1)");
                }
                finally { Marshal.FreeHGlobal(memory); }

                uint actualBytes;
                uint requestedBytes = checked(targetRate * (uint)targetFrameBytes / 10U);
                IntPtr cyclicBuffer = AllocateBuffer(pin, requestedBytes, out actualBytes);
                if (actualBytes % NOTIFICATION_COUNT != 0)
                    throw new InvalidOperationException("WaveRT buffer is not evenly packetized");
                uint packetBytes = actualBytes / NOTIFICATION_COUNT;
                notificationEvent = CreateEvent(IntPtr.Zero, false, false, null);
                if (notificationEvent == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateEvent");
                RegisterEvent(pin, notificationEvent);

                Check(audioClient.Start(), "IAudioClient.Start(loopback)");
                captureStarted = true;
                DateTime prefillUntil = DateTime.UtcNow.AddMilliseconds(130);
                while (DateTime.UtcNow < prefillUntil)
                {
                    Thread.Sleep(5);
                    sourceFrames += DrainCapture(capture, format, resampler);
                }
                pcm.ReadTo(cyclicBuffer, checked((int)actualBytes));

                SetWritePacket(pin, 0);
                SetState(pin, 1);
                SetState(pin, 2);
                SetState(pin, 3);
                pinRunning = true;
                DateTime started = DateTime.UtcNow;
                DateTime nextLog = started;
                DateTime lastNotification = started;
                uint lastWritePacket = 0;
                Console.WriteLine("BRIDGE START source=" + sourceId + " " +
                    format.Description + " -> UAC " + targetRate + "Hz " +
                    targetBits + "bit " + targetChannels + "ch buffer=" + actualBytes +
                    " path=" + path);

                while (seconds <= 0 || (DateTime.UtcNow - started).TotalSeconds < seconds)
                {
                    uint wait = WaitForSingleObject(notificationEvent, 10);
                    sourceFrames += DrainCapture(capture, format, resampler);
                    if (wait == WAIT_OBJECT_0)
                    {
                        lastNotification = DateTime.UtcNow;
                        uint completedPackets = GetPacketCount(pin);
                        uint nextWritePacket = completedPackets + 1;
                        if (nextWritePacket <= lastWritePacket)
                            nextWritePacket = lastWritePacket + 1;
                        uint slot = nextWritePacket % NOTIFICATION_COUNT;
                        IntPtr destination = IntPtr.Add(cyclicBuffer,
                            checked((int)(slot * packetBytes)));
                        pcm.ReadTo(destination, checked((int)packetBytes));
                        SetWritePacket(pin, nextWritePacket);
                        lastWritePacket = nextWritePacket;
                        packetsSubmitted++;
                    }
                    else if (wait != WAIT_TIMEOUT)
                        throw new Win32Exception(Marshal.GetLastWin32Error(),
                            "WaitForSingleObject(WaveRT)");
                    else if ((DateTime.UtcNow - lastNotification).TotalMilliseconds > 750.0)
                        throw new TimeoutException(
                            "WaveRT notifications stopped; the USB stream was reconfigured or disconnected");

                    if (DateTime.UtcNow >= nextLog)
                    {
                        Console.WriteLine("BRIDGE RUN seconds=" +
                            (DateTime.UtcNow - started).TotalSeconds.ToString("F1") +
                            " source_frames=" + sourceFrames +
                            " pcm_frames=" + pcm.ProducedFrames +
                            " queued=" + pcm.AvailableBytes + "B packets=" +
                            packetsSubmitted + " underflow=" + pcm.UnderflowBytes +
                            "B dropped=" + pcm.DroppedBytes + "B peak=" +
                            resampler.Peak.ToString("F4") + " rms=" +
                            resampler.Rms.ToString("F4"));
                        nextLog = DateTime.UtcNow.AddSeconds(Math.Max(1, logIntervalSeconds));
                    }
                }
            }
        }
        finally
        {
            if (pinRunning)
            {
                try { SetState(pin, 0); } catch { }
            }
            if (captureStarted)
            {
                try { audioClient.Stop(); } catch { }
            }
            if (pin != null) pin.Dispose();
            if (notificationEvent != IntPtr.Zero) CloseHandle(notificationEvent);
            Console.WriteLine("BRIDGE STOP source_frames=" + sourceFrames +
                " pcm_frames=" + pcm.ProducedFrames + " packets=" + packetsSubmitted +
                " underflow=" + pcm.UnderflowBytes + "B dropped=" +
                pcm.DroppedBytes + "B peak=" + resampler.Peak.ToString("F4") +
                " rms=" + resampler.Rms.ToString("F4"));
        }
    }
}
'@

Add-Type -TypeDefinition $source -Language CSharp

function Get-FeatherUacFormat {
    param([string]$Port)
    $serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
    $serial.ReadTimeout = 50
    $serial.WriteTimeout = 500
    $serial.NewLine = "`r`n"
    try {
        $serial.Open()
        Start-Sleep -Milliseconds 120
        $serial.DiscardInBuffer()
        $serial.Write("feather_usb status`r`n")
        $deadline = [DateTime]::UtcNow.AddMilliseconds(900)
        $response = ''
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 30
            $response += $serial.ReadExisting()
            if ($response -match 'UAC2 out:\s+(\d+) Hz\s+(\d+)-bit\s+(\d+)-ch') {
                return @([uint32]$Matches[1], [int]$Matches[2], [int]$Matches[3])
            }
        }
        throw "No UAC2 output format was returned by $Port"
    }
    finally {
        if ($serial.IsOpen) { $serial.Close() }
        $serial.Dispose()
    }
}

while ($true) {
    try {
        if ($FollowDeviceFormat) {
            $deviceFormat = Get-FeatherUacFormat -Port $ControlPort
            $TargetSampleRate = $deviceFormat[0]
            $TargetBits = $deviceFormat[1]
            $TargetChannels = $deviceFormat[2]
            Write-Output "BRIDGE TARGET device=$TargetSampleRate Hz $TargetBits-bit $TargetChannels-ch"
        }
        [FeatherDefaultAudioBridge]::Run($DevicePath, $Seconds,
            $LogIntervalSeconds, $TargetSampleRate, $TargetBits,
            $TargetChannels)
        break
    }
    catch {
        if (-not $Reconnect) { throw }
        Write-Output "BRIDGE RECONNECT $($_.Exception.Message)"
        Start-Sleep -Seconds 1
    }
}
