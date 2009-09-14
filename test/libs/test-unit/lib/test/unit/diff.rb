# port of Python's difflib.

module Test
  module Unit
    module Diff
      class SequenceMatcher
        def initialize(from, to, &junk_predicate)
          @from = from
          @to = to
          @junk_predicate = junk_predicate
          update_to_indexes
        end

        def longest_match(from_start, from_end, to_start, to_end)
          best_info = find_best_match_position(from_start, from_end,
                                               to_start, to_end)
          unless @junks.empty?
            args = [from_start, from_end, to_start, to_end]
            best_info = adjust_best_info_with_junk_predicate(false, best_info,
                                                             *args)
            best_info = adjust_best_info_with_junk_predicate(true, best_info,
                                                             *args)
          end

          best_info
        end

        def blocks
          @blocks ||= compute_blocks
        end

        def operations
          @operations ||= compute_operations
        end

        def grouped_operations(context_size=nil)
          context_size ||= 3
          _operations = operations
          _operations = [[:equal, 0, 0, 0, 0]] if _operations.empty?
          expand_edge_equal_operations!(_operations, context_size)

          group_window = context_size * 2
          groups = []
          group = []
          _operations.each do |tag, from_start, from_end, to_start, to_end|
            if tag == :equal and from_end - from_start > group_window
              group << [tag,
                        from_start,
                        [from_end, from_start + context_size].min,
                        to_start,
                        [to_end, to_start + context_size].min]
              groups << group
              group = []
              from_start = [from_start, from_end - context_size].max
              to_start = [to_start, to_end - context_size].max
            end
            group << [tag, from_start, from_end, to_start, to_end]
          end
          groups << group unless group.empty?
          groups
        end

        def ratio
          @ratio ||= compute_ratio
        end

        private
        def update_to_indexes
          @to_indexes = {}
          @junks = {}
          if @to.is_a?(String)
            each = " "[0].is_a?(Integer) ? :each_byte : :each_char
          else
            each = :each
          end
          i = 0
          @to.send(each) do |item|
            @to_indexes[item] ||= []
            @to_indexes[item] << i
            i += 1
          end

          return if @junk_predicate.nil?
          @to_indexes = @to_indexes.reject do |key, value|
            junk = @junk_predicate.call(key)
            @junks[key] = true if junk
            junk
          end
        end

        def find_best_match_position(from_start, from_end, to_start, to_end)
          best_from, best_to, best_size = from_start, to_start, 0
          sizes = {}
          from_start.upto(from_end) do |from_index|
            _sizes = {}
            (@to_indexes[@from[from_index]] || []).each do |to_index|
              next if to_index < to_start
              break if to_index > to_end
              size = _sizes[to_index] = (sizes[to_index - 1] || 0) + 1
              if size > best_size
                best_from = from_index - size + 1
                best_to = to_index - size + 1
                best_size = size
              end
            end
            sizes = _sizes
          end
          [best_from, best_to, best_size]
        end

        def adjust_best_info_with_junk_predicate(should_junk, best_info,
                                                 from_start, from_end,
                                                 to_start, to_end)
          best_from, best_to, best_size = best_info
          while best_from > from_start and best_to > to_start and
              (should_junk ?
               @junks.has_key?(@to[best_to - 1]) :
               !@junks.has_key?(@to[best_to - 1])) and
              @from[best_from - 1] == @to[best_to - 1]
            best_from -= 1
            best_to -= 1
            best_size += 1
          end

          while best_from + best_size < from_end and
              best_to + best_size < to_end and
              (should_junk ?
               @junks.has_key?(@to[best_to + best_size]) :
               !@junks.has_key?(@to[best_to + best_size])) and
              @from[best_from + best_size] == @to[best_to + best_size]
            best_size += 1
          end

          [best_from, best_to, best_size]
        end

        def matches
          @matches ||= compute_matches
        end

        def compute_matches
          matches = []
          queue = [[0, @from.size, 0, @to.size]]
          until queue.empty?
            from_start, from_end, to_start, to_end = queue.pop
            match = longest_match(from_start, from_end - 1, to_start, to_end - 1)
            match_from_index, match_to_index, size = match
            unless size.zero?
              if from_start < match_from_index and
                  to_start < match_to_index
                queue.push([from_start, match_from_index,
                            to_start, match_to_index])
              end
              matches << match
              if match_from_index + size < from_end and
                  match_to_index + size < to_end
                queue.push([match_from_index + size, from_end,
                            match_to_index + size, to_end])
              end
            end
          end
          matches.sort_by do |(from_index, to_index, match_size)|
            from_index
          end
        end

        def compute_blocks
          blocks = []
          current_from_index = current_to_index = current_size = 0
          matches.each do |from_index, to_index, size|
            if current_from_index + current_size == from_index and
                current_to_index + current_size == to_index
              current_size += size
            else
              unless current_size.zero?
                blocks << [current_from_index, current_to_index, current_size]
              end
              current_from_index = from_index
              current_to_index = to_index
              current_size = size
            end
          end
          unless current_size.zero?
            blocks << [current_from_index, current_to_index, current_size]
          end

          blocks << [@from.size, @to.size, 0]
          blocks
        end

        def compute_operations
          from_index = to_index = 0
          operations = []
          blocks.each do |match_from_index, match_to_index, size|
            tag = determine_tag(from_index, to_index,
                                match_from_index, match_to_index)
            if tag != :equal
              operations << [tag,
                             from_index, match_from_index,
                             to_index, match_to_index]
            end

            from_index, to_index = match_from_index + size, match_to_index + size
            if size > 0
              operations << [:equal,
                             match_from_index, from_index,
                             match_to_index, to_index]
            end
          end
          operations
        end

        def compute_ratio
          matches = blocks.inject(0) {|result, block| result + block[-1]}
          length = @from.length + @to.length
          if length.zero?
            1.0
          else
            2.0 * matches / length
          end
        end

        def determine_tag(from_index, to_index,
                          match_from_index, match_to_index)
          if from_index < match_from_index and to_index < match_to_index
            :replace
          elsif from_index < match_from_index
            :delete
          elsif to_index < match_to_index
            :insert
          else
            :equal
          end
        end

        def expand_edge_equal_operations!(_operations, context_size)
          tag, from_start, from_end, to_start, to_end = _operations[0]
          if tag == :equal
            _operations[0] = [tag,
                              [from_start, from_end - context_size].max,
                              from_end,
                              [to_start, to_end - context_size].max,
                              to_end]
          end

          tag, from_start, from_end, to_start, to_end = _operations[-1]
          if tag == :equal
            _operations[-1] = [tag,
                               from_start,
                               [from_end, from_start + context_size].min,
                               to_start,
                               [to_end, to_start + context_size].min]
          end
        end
      end

      class Differ
        def initialize(from, to)
          @from = from
          @to = to
        end

        private
        def tag(mark, contents)
          contents.collect {|content| "#{mark}#{content}"}
        end
      end

      class ReadableDiffer < Differ
        def diff(options={})
          result = []
          matcher = SequenceMatcher.new(@from, @to)
          matcher.operations.each do |args|
            tag, from_start, from_end, to_start, to_end = args
            case tag
            when :replace
              result.concat(diff_lines(from_start, from_end, to_start, to_end))
            when :delete
              result.concat(tag_deleted(@from[from_start...from_end]))
            when :insert
              result.concat(tag_inserted(@to[to_start...to_end]))
            when :equal
              result.concat(tag_equal(@from[from_start...from_end]))
            else
              raise "unknown tag: #{tag}"
            end
          end
          result
        end

        private
        def tag_deleted(contents)
          tag("- ", contents)
        end

        def tag_inserted(contents)
          tag("+ ", contents)
        end

        def tag_equal(contents)
          tag("  ", contents)
        end

        def tag_difference(contents)
          tag("? ", contents)
        end

        def find_diff_line_info(from_start, from_end, to_start, to_end)
          best_ratio = 0.74
          from_equal_index = to_equal_index = nil
          from_best_index = to_best_index = nil

          to_start.upto(to_end - 1) do |to_index|
            from_start.upto(from_end - 1) do |from_index|
              if @from[from_index] == @to[to_index]
                from_equal_index ||= from_index
                to_equal_index ||= to_index
                next
              end

              matcher = SequenceMatcher.new(@from[from_index], @to[to_index],
                                            &method(:space_character?))
              if matcher.ratio > best_ratio
                best_ratio = matcher.ratio
                from_best_index = from_index
                to_best_index = to_index
              end
            end
          end

          [best_ratio,
           from_equal_index, to_equal_index,
           from_best_index,  to_best_index]
        end

        def diff_lines(from_start, from_end, to_start, to_end)
          cut_off = 0.75

          info = find_diff_line_info(from_start, from_end, to_start, to_end)
          best_ratio, from_equal_index, to_equal_index, *info = info
          from_best_index, to_best_index = info

          if best_ratio < cut_off
            if from_equal_index.nil?
              tagged_from = tag_deleted(@from[from_start...from_end])
              tagged_to = tag_inserted(@to[to_start...to_end])
              if to_end - to_start < from_end - from_start
                return tagged_to + tagged_from
              else
                return tagged_from + tagged_to
              end
            end
            from_best_index = from_equal_index
            to_best_index = to_equal_index
            best_ratio = 1.0
          end

          _diff_lines(from_start, from_best_index, to_start, to_best_index) +
            diff_line(@from[from_best_index], @to[to_best_index]) +
            _diff_lines(from_best_index + 1, from_end, to_best_index + 1, to_end)
        end

        def _diff_lines(from_start, from_end, to_start, to_end)
          if from_start < from_end
            if to_start < to_end
              diff_lines(from_start, from_end, to_start, to_end)
            else
              tag_deleted(@from[from_start...from_end])
            end
          else
            tag_inserted(@to[to_start...to_end])
          end
        end

        def diff_line(from_line, to_line)
          from_tags = ""
          to_tags = ""
          matcher = SequenceMatcher.new(from_line, to_line,
                                        &method(:space_character?))
          operations = matcher.operations
          operations.each do |tag, from_start, from_end, to_start, to_end|
            from_length = from_end - from_start
            to_length = to_end - to_start
            case tag
            when :replace
              from_tags << "^" * from_length
              to_tags << "^" * to_length
            when :delete
              from_tags << "-" * from_length
            when :insert
              to_tags << "+" * to_length
            when :equal
              from_tags << " " * from_length
              to_tags << " " * to_length
            else
              raise "unknown tag: #{tag}"
            end
          end
          format_diff_point(from_line, to_line, from_tags, to_tags)
        end

        def format_diff_point(from_line, to_line, from_tags, to_tags)
          common = [n_leading_characters(from_line, ?\t),
                    n_leading_characters(to_line, ?\t)].min
          common = [common,
                    n_leading_characters(from_tags[0, common], " "[0])].min
          from_tags = from_tags[common..-1].rstrip
          to_tags = to_tags[common..-1].rstrip

          result = tag_deleted([from_line])
          unless from_tags.empty?
            result.concat(tag_difference(["#{"\t" * common}#{from_tags}"]))
          end
          result.concat(tag_inserted([to_line]))
          unless to_tags.empty?
            result.concat(tag_difference(["#{"\t" * common}#{to_tags}"]))
          end
          result
        end

        def n_leading_characters(string, character)
          n = 0
          while string[n] == character
            n += 1
          end
          n
        end

        def space_character?(character)
          [" "[0], "\t"[0]].include?(character)
        end
      end

      class UnifiedDiffer < Differ
        def diff(options={})
          groups = SequenceMatcher.new(@from, @to).grouped_operations
          return [] if groups.empty?
          return [] if same_content?(groups)

          show_context = options[:show_context]
          show_context = true if show_context.nil?
          result = ["--- #{options[:from_label]}".rstrip,
                    "+++ #{options[:to_label]}".rstrip]
          groups.each do |operations|
            result << format_summary(operations, show_context)
            operations.each do |args|
              operation_tag, from_start, from_end, to_start, to_end = args
              case operation_tag
              when :replace
                result.concat(tag("-", @from[from_start...from_end]))
                result.concat(tag("+", @to[to_start...to_end]))
              when :delete
                result.concat(tag("-", @from[from_start...from_end]))
              when :insert
                result.concat(tag("+", @to[to_start...to_end]))
              when :equal
                result.concat(tag(" ", @from[from_start...from_end]))
              end
            end
          end
          result
        end

        private
        def same_content?(groups)
          return false if groups.size != 1
          group = groups[0]
          return false if group.size != 1
          tag, from_start, from_end, to_start, to_end = group[0]

          tag == :equal and [from_start, from_end] == [to_start, to_end]
        end

        def format_summary(operations, show_context)
          _, first_from_start, _, first_to_start, _ = operations[0]
          _, _, last_from_end, _, last_to_end = operations[-1]
          summary = "@@ -%d,%d +%d,%d @@" % [first_from_start + 1,
                                             last_from_end - first_from_start,
                                             first_to_start + 1,
                                             last_to_end - first_to_start,]
          if show_context
            interesting_line = find_interesting_line(first_from_start,
                                                     first_to_start,
                                                     :define_line?)
            summary << " #{interesting_line}" if interesting_line
          end
          summary
        end

        def find_interesting_line(from_start, to_start, predicate)
          from_index = from_start
          to_index = to_start
          while from_index >= 0 or to_index >= 0
            [@from[from_index], @to[to_index]].each do |line|
              return line if line and send(predicate, line)
            end

            from_index -= 1
            to_index -= 1
          end
          nil
        end

        def define_line?(line)
          /\A(?:[_a-zA-Z$]|\s*(?:class|module|def)\b)/ =~ line
        end
      end

      module_function
      def need_fold?(diff)
        /^[-+].{79}/ =~ diff
      end

      def fold(string)
        string.split(/\r?\n/).collect do |line|
          line.gsub(/(.{78})/, "\\1\n")
        end.join("\n")
      end

      def folded_readable(from, to, options={})
        readable(fold(from), fold(to), options)
      end

      def readable(from, to, options={})
        diff(ReadableDiffer, from, to, options)
      end

      def unified(from, to, options={})
        diff(UnifiedDiffer, from, to, options)
      end

      def diff(differ_class, from, to, options={})
        differ = differ_class.new(from.split(/\r?\n/), to.split(/\r?\n/))
        differ.diff(options).join("\n")
      end
    end
  end
end
