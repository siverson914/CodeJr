package session

import "testing"

func TestSpeakable(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{"2 + 2398 = 2400", "2 plus 2398 equals 2400"},
		{"2+7=9", "2 plus 7 equals 9"},
		{"You reach me from your PCjr over the WiFi modem.", "You reach me from your PC Junior over the wifi modem."},
		{"I'm CodeJr.", "I'm Code Junior."},
		{"Programs in GW-BASIC, C or assembly.", "Programs in G W Basic, C or assembly."},
		{"Type RUN SNAKE, or load SNAKE.BAS in GWBASIC.", "Type RUN SNAKE, or load snake dot bas in G W Basic."},
		{"It is in C:\\CODEJR\\PROGS now.", "It is in C drive, Code Junior, progs now."},
		{"A 4.77 MHz 8088 with 640K and an SN76496.", "A 4.77 megahertz eighty eighty-eight with 640 K and an S N 7 6 4 9 6."},
		{"An 80x24 screen at 37287 bps.", "An 80 by 24 screen at 37287 bits per second."},
		{"10 - 3 = 7 and 6 * 7 = 42", "10 minus 3 equals 7 and 6 times 7 equals 42"},
		{"About 50% of 12 KB, e.g. 6 KB.", "About 50 percent of 12 kilobytes, for example, 6 kilobytes."},
		{"Type /voice to turn it off.", "Type slash voice to turn it off."},
		{"Pi is 3.14 and that's well-known.", "Pi is 3.14 and that's well-known."},
		{"**Done.** Saved `LOG.TXT`.", "Done. Saved log dot text."},
		{"JRDOOM and JRFLIGHT are games.", "Junior Doom and Junior Flight are games."},
		{"Back at the DOS prompt, on PC DOS 3.30 or MS-DOS.", "Back at the doss prompt, on P C doss 3.30 or M S doss."},
		{"Paris.", "Paris."},
	} {
		if got := Speakable(c.in); got != c.want {
			t.Errorf("Speakable(%q)\n got  %q\n want %q", c.in, got, c.want)
		}
	}
}
