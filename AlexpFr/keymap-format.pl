$_ = keymap_format($_);

sub keymap_format {
    my $line = $_[0];
    my $output = "";
    my $groupsvector;
    my $layervector;
    my @modifiersvector;
    my %organizedvector;


    if ($.== 1) {
        #$output = "keymaps 0-15,64-79\n";
        $output = "keymaps 0-255\n";

    } else {
        my @words = split(/\s+/, $line);  # Divise en mots en supprimant les espaces supplémentaires
        my $count = 0;

        my $word_count = scalar(@words) - 3;  # Compter le nombre de mots à partir du 4ème
        my $num_groups = int($word_count / 16);  # Nombre de groupes de 16 mots

        # Combiner les trois premiers mots avec un seul espace entre eux
        my $first_word = $words[0];
        my $second_word = sprintf("%3s", $words[1]);  # Ajouter des espaces au début si nécessaire pour que la longueur soit de 3 caractères
        my $third_word = $words[2];
        my $first_three_words = join(" ", ($first_word, $second_word, $third_word));
        $output .= sprintf("%-14s", $first_three_words);

        for my $i (3 .. $#words) {
            for my $j (0 .. 15) {
                @modifiersvector[$j] = $words[$i];
                ++$i;
            }
        }

        for my $i (3 .. $#words) {
            for my $group (0 .. 3) {
                for my $state (0 .. 3) {
                    for my $j (0 .. 15) {
                        $organizedvector{$group}{$state}[$j] = $words[$i];
                        ++$i;
                    }
                }
            }

        }




        # Traiter les mots restants
        for my $i (3 .. $#words) {
            if (true ) {
                $output .= sprintf("%-30s", $words[$i]);
            }
            ++$count;
            if ($i >= 3 && $count % 16 == 0) {
                if ($count / 16 == $num_groups) {
                    $output .= "\n";
                } elsif (true) {
                    $output .= " \\\n" . (" " x 14);
                }
            }
        }

        # Si le dernier mot ne termine pas la ligne avec une nouvelle ligne
        $output =~ s/ \\\n {30}$/\n/;
    }

    return $output;
}

END {
    print "\n";  # Ajouter un saut de ligne à la fin du fichier de sortie
}


# perl -p keymap-format.pl fr-ossckb.map > fr-ossckbbnew.map


## Layout Config Table
# 15 Keymaps are reserved per layout (Shift, AltGr, Control, Alt, and all of their combinations)

# Layout | Modifiers                 | Range      | #
# ----------------------------------------------------
#    0   |                           |   0 ->  15 | 0
# ----------------------------------------------------
#    1   | ShiftL                    |  16 ->  31 | 1
#    2   |        ShiftR             |  32 ->  47 | 1
#    3   | ShiftL ShiftR             |  48 ->  63 | 2
#    4   |               CtrlL       |  64 ->  79 | 1
#    5   | ShiftL        CtrlL       |  80 ->  95 | 2
#    6   |        ShiftR CtrlL       |  96 -> 111 | 2
#    7   | ShiftL ShiftR CtrlL       | 112 -> 127 | 3
# ----------------------------------------------------
#    8   |                     CtrlR | 128 -> 143 | 1
#    9   | ShiftL              CtrlR | 144 -> 159 | 2
#   10   |        ShiftR       CtrlR | 160 -> 175 | 2
#   11   | ShiftL ShiftR       CtrlR | 176 -> 191 | 3
#   12   |               CtrlL CtrlR | 192 -> 207 | 2
#   13   | ShiftL        CtrlL CtrlR | 208 -> 223 | 3
#   14   |        ShiftR CtrlL CtrlR | 224 -> 239 | 3
#   15   | ShiftL ShiftR CtrlL CtrlR | 240 -> 255 | 4
