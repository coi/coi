const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// =========================================================
// Coi Formatter
// =========================================================

class CoiFormatter {
    constructor() {
        this.indentSize = 4;
        this.useSpaces = true;
        // Block-level HTML elements that should be on their own lines
        this.blockElements = new Set([
            'div', 'p', 'section', 'article', 'header', 'footer', 'nav', 'main', 'aside',
            'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'ul', 'ol', 'li', 'dl', 'dt', 'dd',
            'table', 'thead', 'tbody', 'tfoot', 'tr', 'td', 'th', 'form', 'fieldset',
            'blockquote', 'pre', 'canvas', 'video', 'audio', 'figure', 'figcaption'
        ]);
    }

    format(text, options = {}) {
        this.indentSize = options.tabSize || 4;
        this.useSpaces = options.insertSpaces !== false;

        const lines = text.split('\n');
        const formattedLines = [];
        let indentLevel = 0;
        let inViewBlock = false;
        let inStyleBlock = false;
        let styleBlockBraceCount = 0;
        let cssIndentLevel = 0;

        for (let i = 0; i < lines.length; i++) {
            let line = lines[i];
            let trimmed = line.trim();

            // Skip empty lines but preserve them
            if (trimmed === '') {
                formattedLines.push('');
                continue;
            }

            // Handle style blocks (CSS formatting with proper indentation)
            if (inStyleBlock) {
                // Count braces in this line
                const openBraces = (trimmed.match(/\{/g) || []).length;
                const closeBraces = (trimmed.match(/\}/g) || []).length;
                
                // Decrease indent if line starts with closing brace
                if (trimmed.startsWith('}')) {
                    cssIndentLevel = Math.max(0, cssIndentLevel - 1);
                }

                styleBlockBraceCount += openBraces - closeBraces;

                if (styleBlockBraceCount <= 0) {
                    // End of style block
                    inStyleBlock = false;
                    indentLevel = Math.max(0, indentLevel - 1);
                    formattedLines.push(this.indent(indentLevel) + trimmed);
                    styleBlockBraceCount = 0;
                    cssIndentLevel = 0;
                } else {
                    // Inside style block - apply CSS indentation
                    formattedLines.push(this.indent(indentLevel + cssIndentLevel) + trimmed);
                    
                    // Increase indent if line ends with opening brace
                    if (trimmed.endsWith('{')) {
                        cssIndentLevel++;
                    }
                }
                continue;
            }

            // Check for style block start
            if (/^style\s*(global)?\s*\{/.test(trimmed)) {
                formattedLines.push(this.indent(indentLevel) + this.formatStyleDeclaration(trimmed));
                inStyleBlock = true;
                styleBlockBraceCount = 1;
                indentLevel++;
                continue;
            }

            // Check for view block start
            if (/^view\s*\{/.test(trimmed)) {
                formattedLines.push(this.indent(indentLevel) + trimmed);
                inViewBlock = true;
                indentLevel++;
                continue;
            }

            // Handle end of view block
            if (inViewBlock && trimmed === '}') {
                inViewBlock = false;
                indentLevel = Math.max(0, indentLevel - 1);
                formattedLines.push(this.indent(indentLevel) + trimmed);
                continue;
            }

            // Handle HTML/view block indentation
            if (inViewBlock && trimmed.startsWith('<')) {
                // Break up nested block elements onto separate lines
                const expandedLines = this.expandBlockElements(trimmed);
                
                for (const expandedLine of expandedLines) {
                    const expTrimmed = expandedLine.trim();
                    if (!expTrimmed) continue;
                    
                    // Decrease indent for closing tags BEFORE outputting
                    if (expTrimmed.startsWith('</')) {
                        indentLevel = Math.max(0, indentLevel - 1);
                        formattedLines.push(this.indent(indentLevel) + expTrimmed);
                    } else {
                        // Output the line
                        formattedLines.push(this.indent(indentLevel) + expTrimmed);
                        
                        // Increase indent for opening block tags (not self-closing)
                        const netIndent = this.calculateNetIndent(expTrimmed);
                        indentLevel += netIndent;
                        indentLevel = Math.max(0, indentLevel);
                    }
                }
                continue;
            }

            // Handle regular code (non-style, non-view)
            // Determine if we should decrease indent before this line
            const startsWithClose = /^[\}\]]/.test(trimmed);
            if (startsWithClose && indentLevel > 0) {
                indentLevel--;
            }

            // Format the line
            let formattedLine = this.formatLine(trimmed, inViewBlock);
            formattedLines.push(this.indent(indentLevel) + formattedLine);

            // Determine if we should increase indent after this line
            const endsWithOpen = /[\{\[]$/.test(trimmed) && !/\}$/.test(trimmed);
            if (endsWithOpen) {
                indentLevel++;
            }

            // Ensure indent level doesn't go negative
            indentLevel = Math.max(0, indentLevel);
        }

        return formattedLines.join('\n');
    }

    formatLine(line, inView) {
        // Don't format comments
        if (line.startsWith('//')) {
            return line;
        }

        // Format component declaration
        if (line.startsWith('component ')) {
            return this.formatComponentDeclaration(line);
        }

        // Format function declaration
        if (line.startsWith('def ')) {
            return this.formatFunctionDeclaration(line);
        }

        // Don't try to format lines in view blocks (HTML-like content)
        if (inView && (line.startsWith('<') || line.includes('{') || line.includes('}'))) {
            return line;
        }

        // Format variable declarations
        if (/^(pub\s+)?(mut\s+)?(\w+)\s+\w+/.test(line)) {
            return this.formatVariableDeclaration(line);
        }

        // Format operators with proper spacing
        line = this.formatOperators(line);

        return line;
    }

    formatComponentDeclaration(line) {
        // component Name(params) {
        return line
            .replace(/component\s+/, 'component ')
            .replace(/\(\s*/g, '(')
            .replace(/\s*\)/g, ')')
            .replace(/\s*,\s*/g, ', ')
            .replace(/\s*\{\s*$/, ' {');
    }

    formatFunctionDeclaration(line) {
        // def name(params) : returnType {
        return line
            .replace(/def\s+/, 'def ')
            .replace(/\(\s*/g, '(')
            .replace(/\s*\)/g, ')')
            .replace(/\s*,\s*/g, ', ')
            .replace(/\s*:\s*/g, ' : ')
            .replace(/\s*\{\s*$/, ' {');
    }

    formatVariableDeclaration(line) {
        // pub mut Type name = value;
        return line
            .replace(/pub\s+/, 'pub ')
            .replace(/mut\s+/, 'mut ')
            .replace(/\s*=\s*/g, ' = ')
            .replace(/\s*;\s*$/, ';');
    }

    formatStyleDeclaration(line) {
        return line
            .replace(/style\s*(global)?\s*\{/, (m, global) => global ? 'style global {' : 'style {');
    }

    formatOperators(line) {
        // Don't format inside strings or braces (could be expressions)
        const protectedList = [];
        let protectedIndex = 0;

        // Protect strings
        line = line.replace(/"[^"]*"/g, (match) => {
            protectedList.push(match);
            return `__PROTECTED_${protectedIndex++}__`;
        });

        // Protect expressions in braces
        line = line.replace(/\{[^}]*\}/g, (match) => {
            protectedList.push(match);
            return `__PROTECTED_${protectedIndex++}__`;
        });

        // Format operators
        line = line
            // Move assignment operator := (keep together)
            .replace(/\s*:=\s*/g, ' := ')
            // Comparison operators
            .replace(/\s*==\s*/g, ' == ')
            .replace(/\s*!=\s*/g, ' != ')
            .replace(/\s*<=\s*/g, ' <= ')
            .replace(/\s*>=\s*/g, ' >= ')
            // Logical operators
            .replace(/\s*&&\s*/g, ' && ')
            .replace(/\s*\|\|\s*/g, ' || ')
            // Shift operators (before single < or >)
            .replace(/\s*<<=/g, ' <<= ')
            .replace(/\s*>>=/g, ' >>= ')
            .replace(/\s*<</g, ' << ')
            .replace(/\s*>>/g, ' >> ')
            // Bitwise compound assignment
            .replace(/\s*&=\s*/g, ' &= ')
            .replace(/\s*\|=\s*/g, ' |= ')
            .replace(/\s*\^=\s*/g, ' ^= ')
            // Assignment operators (but not :=)
            .replace(/\s*\+=\s*/g, ' += ')
            .replace(/\s*-=\s*/g, ' -= ')
            .replace(/\s*\*=\s*/g, ' *= ')
            .replace(/\s*\/=\s*/g, ' /= ')
            // Bitwise operators (with spaces around binary use)
            .replace(/\s*\|\s*/g, ' | ')
            .replace(/\s*\^\s*/g, ' ^ ')
            // Simple assignment (but not == or != or := or &= etc.)
            .replace(/([^=!<>+\-*/:&|^])\s*=\s*([^=])/g, '$1 = $2');

        // Restore protected sections
        protectedList.forEach((str, i) => {
            line = line.replace(`__PROTECTED_${i}__`, str);
        });

        return line;
    }

    indent(level) {
        const char = this.useSpaces ? ' ' : '\t';
        const size = this.useSpaces ? this.indentSize : 1;
        return char.repeat(level * size);
    }

    calculateNetIndent(line) {
        // Count opening and closing tags on this line
        const selfClosingCount = (line.match(/<[^>]+\/>/g) || []).length;
        const openingCount = (line.match(/<[a-zA-Z][^>]*>/g) || []).length;
        const closingCount = (line.match(/<\/[^>]+>/g) || []).length;
        
        // Net change in indent (opening tags that aren't self-closing, minus closing tags)
        return (openingCount - selfClosingCount) - closingCount;
    }

    isBlockElement(tagName) {
        return this.blockElements.has(tagName.toLowerCase());
    }

    expandBlockElements(line) {
        // Check if line has nested block elements that should be split
        // Pattern: <footer><p>content</p></footer> -> split block elements to separate lines
        const tagRegex = /<\/?([a-zA-Z][a-zA-Z0-9-]*)[^>]*>/g;
        const tags = [];
        let match;
        
        while ((match = tagRegex.exec(line)) !== null) {
            const fullTag = match[0];
            const tagName = match[1];
            const isClosing = fullTag.startsWith('</');
            const isSelfClosing = fullTag.endsWith('/>');
            tags.push({ 
                tag: fullTag, 
                name: tagName, 
                isClosing, 
                isSelfClosing,
                index: match.index,
                endIndex: match.index + fullTag.length
            });
        }

        // Only expand if we have multiple BLOCK elements (not inline like <a>, <span>)
        const blockTags = tags.filter(t => this.isBlockElement(t.name));
        if (blockTags.length <= 1) {
            return [line]; // No expansion needed - single block or all inline
        }

        // Special case: Don't expand if it's just a single block element wrapping content
        // e.g. <div class="logo">.coi</div> or <h1>Title</h1>
        if (blockTags.length === 2 && 
            !blockTags[0].isClosing && 
            blockTags[1].isClosing && 
            blockTags[0].name === blockTags[1].name) {
            return [line];
        }

        // Split: output block tags on their own lines, keeping inline content together
        const result = [];
        let pos = 0;
        
        for (let i = 0; i < blockTags.length; i++) {
            const tag = blockTags[i];
            
            // Content between last position and this block tag
            const contentBefore = line.substring(pos, tag.index).trim();
            
            if (tag.isClosing) {
                // For closing block tag: output content first, then the tag
                if (contentBefore) {
                    result.push(contentBefore);
                }
                result.push(tag.tag);
            } else {
                // Opening block tag: output content first, then the tag
                if (contentBefore) {
                    result.push(contentBefore);
                }
                result.push(tag.tag);
            }
            
            pos = tag.endIndex;
        }

        // Any remaining content after the last block tag
        const remaining = line.substring(pos).trim();
        if (remaining) {
            result.push(remaining);
        }
        
        return result.length > 0 ? result : [line];
    }

}

// =========================================================
// Coi Definition Parser
// =========================================================

class CoiDefinitions {
    constructor() {
        this.types = new Map();      // type Name { ... }
        this.namespaces = new Map(); // namespace Name { ... }
        this.components = new Map(); // component Name { ... } from user files
        this.defBasePath = null;     // Base path for def files (cached)
    }

    // Parse all .d.coi files from a directory (recursively)
    loadFromDirectory(defPath) {
        if (!fs.existsSync(defPath)) return;

        // Cache the base path for later use
        this.defBasePath = defPath;

        const loadRecursive = (dir) => {
            const entries = fs.readdirSync(dir, { withFileTypes: true });
            for (const entry of entries) {
                const fullPath = path.join(dir, entry.name);
                if (entry.isDirectory()) {
                    loadRecursive(fullPath);
                } else if (entry.name.endsWith('.d.coi')) {
                    const content = fs.readFileSync(fullPath, 'utf8');
                    // Pass the full absolute path as source
                    this.parseDefinitionFile(content, fullPath);
                }
            }
        };

        loadRecursive(defPath);
    }

    // Parse a single .d.coi file
    parseDefinitionFile(content, sourcePath) {
        const lines = content.split('\n');
        let currentBlock = null;  // { kind: 'type'|'namespace', name: string, methods: [] }
        let braceDepth = 0;

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const trimmed = line.trim();
            const lineNumber = i + 1;  // 1-based line number

            // Skip comments and empty lines
            if (trimmed.startsWith('//') || trimmed === '') continue;

            // Match type or namespace declaration
            const typeMatch = trimmed.match(/^type\s+(\w+)(?:\s+extends\s+(\w+))?\s*\{/);
            const nsMatch = trimmed.match(/^namespace\s+(\w+)\s*\{/);

            if (typeMatch) {
                currentBlock = {
                    kind: 'type',
                    name: typeMatch[1],
                    extends: typeMatch[2] || null,
                    methods: [],
                    typeMethods: [],  // type def (static)
                    source: sourcePath,  // Full absolute path
                    line: lineNumber
                };
                braceDepth = 1;
                continue;
            }

            if (nsMatch) {
                currentBlock = {
                    kind: 'namespace',
                    name: nsMatch[1],
                    methods: [],
                    source: sourcePath,  // Full absolute path
                    line: lineNumber
                };
                braceDepth = 1;
                continue;
            }

            if (currentBlock) {
                // Track braces
                braceDepth += (line.match(/\{/g) || []).length;
                braceDepth -= (line.match(/\}/g) || []).length;

                // Parse method definitions
                // type def methodName(...): ReturnType { ... }
                const typeDefMatch = trimmed.match(/^type\s+def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                // def methodName(...): ReturnType { ... }
                const defMatch = trimmed.match(/^def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                // // maps to: ns::func
                const mapsToMatch = trimmed.match(/\/\/\s*maps to:\s*(\w+)::(\w+)/);

                if (typeDefMatch) {
                    const method = {
                        name: typeDefMatch[1],
                        params: this.parseParams(typeDefMatch[2]),
                        returnType: typeDefMatch[3],
                        isTypeMethod: true,
                        mapsTo: null,
                        line: lineNumber
                    };
                    currentBlock.typeMethods.push(method);
                    currentBlock._lastMethod = method;
                } else if (defMatch) {
                    const method = {
                        name: defMatch[1],
                        params: this.parseParams(defMatch[2]),
                        returnType: defMatch[3],
                        isTypeMethod: false,
                        mapsTo: null,
                        line: lineNumber
                    };
                    currentBlock.methods.push(method);
                    currentBlock._lastMethod = method;
                } else if (mapsToMatch && currentBlock._lastMethod) {
                    currentBlock._lastMethod.mapsTo = `${mapsToMatch[1]}::${mapsToMatch[2]}`;
                }

                // End of block
                if (braceDepth === 0) {
                    delete currentBlock._lastMethod;
                    if (currentBlock.kind === 'type') {
                        this.types.set(currentBlock.name, currentBlock);
                    } else {
                        this.namespaces.set(currentBlock.name, currentBlock);
                    }
                    currentBlock = null;
                }
            }
        }
    }

    // Parse parameter list: "x: int, y: float" -> [{name: 'x', type: 'int'}, ...]
    parseParams(paramStr) {
        if (!paramStr.trim()) return [];
        return paramStr.split(',').map(p => {
            const [name, type] = p.split(':').map(s => s.trim());
            return { name, type };
        });
    }

    // Parse a user .coi file to extract components
    parseUserFile(content) {
        const components = new Map();
        const lines = content.split('\n');
        let currentComponent = null;
        let braceDepth = 0;

        for (const line of lines) {
            const trimmed = line.trim();

            // component Name {
            const compMatch = trimmed.match(/^component\s+(\w+)\s*\{/);
            if (compMatch) {
                currentComponent = {
                    name: compMatch[1],
                    props: [],
                    state: [],
                    methods: []
                };
                braceDepth = 1;
                continue;
            }

            if (currentComponent) {
                braceDepth += (line.match(/\{/g) || []).length;
                braceDepth -= (line.match(/\}/g) || []).length;

                // prop mut? type& name;
                const propMatch = trimmed.match(/^prop\s+(mut\s+)?(\w+)(&)?\s+(\w+)/);
                if (propMatch) {
                    currentComponent.props.push({
                        name: propMatch[4],
                        type: propMatch[2],
                        mutable: !!propMatch[1],
                        reference: !!propMatch[3]
                    });
                }

                // mut? type name = ...;
                const stateMatch = trimmed.match(/^(mut\s+)?(\w+)\s+(\w+)\s*=/);
                if (stateMatch && !trimmed.startsWith('prop')) {
                    currentComponent.state.push({
                        name: stateMatch[3],
                        type: stateMatch[2],
                        mutable: !!stateMatch[1]
                    });
                }

                // def name(...) : type {
                const methodMatch = trimmed.match(/^def\s+(\w+)\s*\(([^)]*)\)\s*:\s*(\w+)/);
                if (methodMatch) {
                    currentComponent.methods.push({
                        name: methodMatch[1],
                        params: this.parseParams(methodMatch[2]),
                        returnType: methodMatch[3]
                    });
                }

                if (braceDepth === 0) {
                    components.set(currentComponent.name, currentComponent);
                    currentComponent = null;
                }
            }
        }

        return components;
    }
}

// =========================================================
// VS Code Extension
// =========================================================

let definitions = new CoiDefinitions();
const formatter = new CoiFormatter();

function activate(context) {
    console.log('Coi Language extension activated');

    // Load definitions from bundled defs/ folder or custom path
    loadDefinitions(context);

    // Register completion provider
    const completionProvider = vscode.languages.registerCompletionItemProvider(
        'coi',
        {
            provideCompletionItems(document, position) {
                return getCompletions(document, position);
            }
        },
        '.', // Trigger on dot
        '<'  // Trigger on < for view element tags
    );

    // Register hover provider
    const hoverProvider = vscode.languages.registerHoverProvider('coi', {
        provideHover(document, position) {
            return getHover(document, position);
        }
    });

    // Register signature help
    const signatureProvider = vscode.languages.registerSignatureHelpProvider(
        'coi',
        {
            provideSignatureHelp(document, position) {
                return getSignatureHelp(document, position);
            }
        },
        '(', ','
    );

    // Register definition provider (Ctrl+Click go to definition)
    const definitionProvider = vscode.languages.registerDefinitionProvider('coi', {
        provideDefinition(document, position) {
            return getDefinition(document, position);
        }
    });

    // Register document formatter
    const formattingProvider = vscode.languages.registerDocumentFormattingEditProvider('coi', {
        provideDocumentFormattingEdits(document) {
            const text = document.getText();
            const options = {
                tabSize: vscode.workspace.getConfiguration('editor').get('tabSize', 4),
                insertSpaces: vscode.workspace.getConfiguration('editor').get('insertSpaces', true)
            };
            const formatted = formatter.format(text, options);

            if (formatted === text) {
                return [];
            }

            const fullRange = new vscode.Range(
                document.positionAt(0),
                document.positionAt(text.length)
            );
            return [vscode.TextEdit.replace(fullRange, formatted)];
        }
    });

    // Register range formatter
    const rangeFormattingProvider = vscode.languages.registerDocumentRangeFormattingEditProvider('coi', {
        provideDocumentRangeFormattingEdits(document, range) {
            const text = document.getText(range);
            const options = {
                tabSize: vscode.workspace.getConfiguration('editor').get('tabSize', 4),
                insertSpaces: vscode.workspace.getConfiguration('editor').get('insertSpaces', true)
            };
            const formatted = formatter.format(text, options);

            if (formatted === text) {
                return [];
            }

            return [vscode.TextEdit.replace(range, formatted)];
        }
    });

    context.subscriptions.push(
        completionProvider,
        hoverProvider,
        signatureProvider,
        definitionProvider,
        formattingProvider,
        rangeFormattingProvider
    );
}

function loadDefinitions(context) {
    const config = vscode.workspace.getConfiguration('coi');
    let defPath = config.get('definitionsPath');

    if (!defPath) {
        // Dynamically discover def path using 'coi --defs-path'
        try {
            const output = execSync('coi --defs-path', { encoding: 'utf8' });
            defPath = output.trim();
            if (!fs.existsSync(defPath)) {
                throw new Error('Returned defs path does not exist: ' + defPath);
            }
        } catch (err) {
            vscode.window.showErrorMessage('Failed to locate Coi definitions directory using \'coi --defs-path\'. Please set \'coi.definitionsPath\' in your settings.');
            return;
        }
    }

    definitions = new CoiDefinitions();
    definitions.loadFromDirectory(defPath);
    console.log(`Loaded ${definitions.types.size} types, ${definitions.namespaces.size} namespaces from ${defPath}`);
}

function getCompletions(document, position) {
    const items = [];
    const lineText = document.lineAt(position).text;
    const textBefore = lineText.substring(0, position.character);

    // Check if we're after a dot (method completion)
    const dotMatch = textBefore.match(/(\w+)\.\s*(\w*)$/);
    if (dotMatch) {
        const typeName = dotMatch[1];
        const partial = dotMatch[2] || '';

        // Check if it's a known type (for type methods like Canvas.createCanvas)
        const typeInfo = definitions.types.get(typeName);
        if (typeInfo) {
            // Add type methods (static)
            for (const method of typeInfo.typeMethods || []) {
                if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                    const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Method);
                    item.detail = `${typeName}.${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                    item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                    item.insertText = new vscode.SnippetString(
                        method.name + '(' + createSnippetParams(method.params) + ')'
                    );
                    items.push(item);
                }
            }
        }

        // Check if it's a namespace
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            for (const method of nsInfo.methods) {
                if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                    const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Function);
                    item.detail = `${typeName}.${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                    item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                    item.insertText = new vscode.SnippetString(
                        method.name + '(' + createSnippetParams(method.params) + ')'
                    );
                    items.push(item);
                }
            }
        }

        // Check if it's a variable - find its type and show instance methods
        const varType = findVariableType(document, position, typeName);
        if (varType) {
            const typeInfo = definitions.types.get(varType);
            if (typeInfo) {
                for (const method of typeInfo.methods) {
                    if (method.name.toLowerCase().startsWith(partial.toLowerCase())) {
                        const item = new vscode.CompletionItem(method.name, vscode.CompletionItemKind.Method);
                        item.detail = `${method.name}(${formatParams(method.params)}): ${method.returnType}`;
                        item.documentation = method.mapsTo ? `Maps to: ${method.mapsTo}` : '';
                        item.insertText = new vscode.SnippetString(
                            method.name + '(' + createSnippetParams(method.params) + ')'
                        );
                        items.push(item);
                    }
                }
            }
        }

        return items;
    }

    // Check for view element tag completion
    if (textBefore.match(/<(\w*)$/)) {
        // Parse current document for components
        const userComponents = definitions.parseUserFile(document.getText());

        // Add user-defined components
        for (const [name, comp] of userComponents) {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
            item.detail = `component ${name}`;
            const propDesc = comp.props.map(p => `${p.name}: ${p.type}`).join(', ');
            item.documentation = propDesc ? `Props: ${propDesc}` : 'No props';
            items.push(item);
        }

        // Add HTML tags
        const htmlTags = ['div', 'span', 'p', 'h1', 'h2', 'h3', 'button', 'input', 'a', 'img', 'ul', 'li', 'canvas'];
        for (const tag of htmlTags) {
            const item = new vscode.CompletionItem(tag, vscode.CompletionItemKind.Property);
            item.detail = `HTML <${tag}>`;
            items.push(item);
        }

        return items;
    }

    // Top-level completions (types, namespaces, keywords)
    // Keywords
    const keywords = ['component', 'def', 'view', 'style', 'prop', 'mut', 'pub', 'tick', 'init', 'mount', 'if', 'else', 'for', 'while', 'match', 'return', 'import', 'app', 'struct', 'in', 'key'];
    for (const kw of keywords) {
        const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
        items.push(item);
    }

    // Types
    const types = ['int', 'float', 'string', 'bool', 'void'];
    for (const t of types) {
        const item = new vscode.CompletionItem(t, vscode.CompletionItemKind.TypeParameter);
        items.push(item);
    }

    // Handle types from definitions
    for (const [name, info] of definitions.types) {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Class);
        item.detail = info.extends ? `type ${name} extends ${info.extends}` : `type ${name}`;
        items.push(item);
    }

    // Namespaces from definitions
    for (const [name, info] of definitions.namespaces) {
        const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Module);
        item.detail = `namespace ${name}`;
        items.push(item);
    }

    return items;
}

function getHover(document, position) {
    const wordRange = document.getWordRangeAtPosition(position);
    if (!wordRange) return null;

    const word = document.getText(wordRange);
    const lineText = document.lineAt(position).text;

    // Check if hovering over Type.method
    const dotPattern = new RegExp(`(\\w+)\\.${word}\\b`);
    const dotMatch = lineText.match(dotPattern);

    if (dotMatch) {
        const typeName = dotMatch[1];

        // Check type methods
        const typeInfo = definitions.types.get(typeName);
        if (typeInfo) {
            // Check type methods (static)
            const typeMethod = (typeInfo.typeMethods || []).find(m => m.name === word);
            if (typeMethod) {
                return new vscode.Hover([
                    `**${typeName}.${word}** (type method)`,
                    `\`\`\`coi\ntype def ${word}(${formatParams(typeMethod.params)}): ${typeMethod.returnType}\n\`\`\``,
                    typeMethod.mapsTo ? `Maps to: \`${typeMethod.mapsTo}\`` : ''
                ].filter(Boolean));
            }
            // Check instance methods
            const method = typeInfo.methods.find(m => m.name === word);
            if (method) {
                return new vscode.Hover([
                    `**${typeName}.${word}** (instance method)`,
                    `\`\`\`coi\ndef ${word}(${formatParams(method.params)}): ${method.returnType}\n\`\`\``,
                    method.mapsTo ? `Maps to: \`${method.mapsTo}\`` : ''
                ].filter(Boolean));
            }
        }

        // Check namespace
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            const method = nsInfo.methods.find(m => m.name === word);
            if (method) {
                return new vscode.Hover([
                    `**${typeName}.${word}**`,
                    `\`\`\`coi\ndef ${word}(${formatParams(method.params)}): ${method.returnType}\n\`\`\``,
                    method.mapsTo ? `Maps to: \`${method.mapsTo}\`` : ''
                ].filter(Boolean));
            }
        }
    }

    // Check if hovering over a type name
    const typeInfo = definitions.types.get(word);
    if (typeInfo) {
        const methodCount = (typeInfo.typeMethods || []).length + typeInfo.methods.length;
        return new vscode.Hover([
            `**type ${word}**` + (typeInfo.extends ? ` extends ${typeInfo.extends}` : ''),
            `${methodCount} methods`,
            `Source: ${typeInfo.source}`
        ]);
    }

    // Check if hovering over a namespace
    const nsInfo = definitions.namespaces.get(word);
    if (nsInfo) {
        return new vscode.Hover([
            `**namespace ${word}**`,
            `${nsInfo.methods.length} functions`,
            `Source: ${nsInfo.source}`
        ]);
    }

    return null;
}

function getDefinition(document, position) {
    const wordRange = document.getWordRangeAtPosition(position);
    if (!wordRange) return null;

    const word = document.getText(wordRange);
    const lineText = document.lineAt(position).text;

    // Check if it's Type.method or var.method
    const dotPattern = new RegExp(`(\\w+)\\.${word}\\s*\\(`);
    const dotMatch = lineText.match(dotPattern);

    if (dotMatch) {
        const typeName = dotMatch[1];

        // Check type methods
        const typeInfo = definitions.types.get(typeName);
        if (typeInfo) {
            const typeMethod = (typeInfo.typeMethods || []).find(m => m.name === word);
            if (typeMethod && typeInfo.source) {
                return createDefinitionLocation(typeInfo.source, typeMethod.line);
            }
            const method = typeInfo.methods.find(m => m.name === word);
            if (method && typeInfo.source) {
                return createDefinitionLocation(typeInfo.source, method.line);
            }
        }

        // Check namespace
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            const method = nsInfo.methods.find(m => m.name === word);
            if (method && nsInfo.source) {
                return createDefinitionLocation(nsInfo.source, method.line);
            }
        }

        // Check by variable type
        const varType = findVariableType(document, position, typeName);
        if (varType) {
            const varTypeInfo = definitions.types.get(varType);
            if (varTypeInfo) {
                const method = varTypeInfo.methods.find(m => m.name === word);
                if (method && varTypeInfo.source) {
                    return createDefinitionLocation(varTypeInfo.source, method.line);
                }
            }
        }
    }

    // Check if it's a type name directly
    const typeInfo = definitions.types.get(word);
    if (typeInfo && typeInfo.source) {
        return createDefinitionLocation(typeInfo.source, typeInfo.line);
    }

    // Check if it's a namespace
    const nsInfo = definitions.namespaces.get(word);
    if (nsInfo && nsInfo.source) {
        return createDefinitionLocation(nsInfo.source, nsInfo.line);
    }

    // Check if it's a component reference (PascalCase word)
    if (/^[A-Z][A-Za-z0-9_]*$/.test(word)) {
        const componentLocation = findComponentDefinition(word);
        if (componentLocation) {
            return componentLocation;
        }
    }

    return null;
}

// Find component definition in workspace .coi files
function findComponentDefinition(componentName) {
    if (!vscode.workspace.workspaceFolders) return null;

    const workspaceRoot = vscode.workspace.workspaceFolders[0].uri.fsPath;

    // Search for .coi files in workspace
    const coiFiles = findCoiFiles(workspaceRoot);

    for (const filePath of coiFiles) {
        try {
            const content = fs.readFileSync(filePath, 'utf8');
            const lines = content.split('\n');

            for (let i = 0; i < lines.length; i++) {
                const line = lines[i];
                // Match: component ComponentName {
                const match = line.match(/^\s*component\s+(\w+)\s*\{?/);
                if (match && match[1] === componentName) {
                    const uri = vscode.Uri.file(filePath);
                    const pos = new vscode.Position(i, line.indexOf('component'));
                    return new vscode.Location(uri, pos);
                }
            }
        } catch (err) {
            // Skip files that can't be read
        }
    }

    return null;
}

// Recursively find .coi files in a directory
function findCoiFiles(dir, files = []) {
    try {
        const entries = fs.readdirSync(dir, { withFileTypes: true });

        for (const entry of entries) {
            const fullPath = path.join(dir, entry.name);

            // Skip node_modules, build, .git directories
            if (entry.isDirectory()) {
                if (!['node_modules', 'build', '.git', 'dist'].includes(entry.name)) {
                    findCoiFiles(fullPath, files);
                }
            } else if (entry.name.endsWith('.coi') && !entry.name.endsWith('.d.coi')) {
                files.push(fullPath);
            }
        }
    } catch (err) {
        // Skip directories that can't be read
    }

    return files;
}

function createDefinitionLocation(sourcePath, line) {
    // sourcePath is now always the full absolute path (stored during definition loading)
    const uri = vscode.Uri.file(sourcePath);
    const pos = new vscode.Position(Math.max(0, (line || 1) - 1), 0);
    return new vscode.Location(uri, pos);
}

// Recursively find a file by name in a directory (used for component definitions)
function findFileRecursively(dir, filename) {
    try {
        const entries = fs.readdirSync(dir, { withFileTypes: true });
        
        for (const entry of entries) {
            const fullPath = path.join(dir, entry.name);
            
            if (entry.isDirectory()) {
                // Skip node_modules, build, etc.
                if (!['node_modules', 'build', '.git', 'dist'].includes(entry.name)) {
                    const found = findFileRecursively(fullPath, filename);
                    if (found) return found;
                }
            } else if (entry.name === filename) {
                return fullPath;
            }
        }
    } catch (err) {
        // Skip directories that can't be read
    }
    
    return null;
}

function getSignatureHelp(document, position) {
    const lineText = document.lineAt(position).text.substring(0, position.character);

    // Find function call: TypeOrVar.method( or just method(
    const callMatch = lineText.match(/(\w+)\.(\w+)\s*\([^)]*$/);
    if (!callMatch) return null;

    const typeName = callMatch[1];
    const methodName = callMatch[2];

    let method = null;
    let container = typeName;

    // Search in types
    const typeInfo = definitions.types.get(typeName);
    if (typeInfo) {
        method = (typeInfo.typeMethods || []).find(m => m.name === methodName) ||
            typeInfo.methods.find(m => m.name === methodName);
    }

    // Search in namespaces
    if (!method) {
        const nsInfo = definitions.namespaces.get(typeName);
        if (nsInfo) {
            method = nsInfo.methods.find(m => m.name === methodName);
        }
    }

    // Search by variable type
    if (!method) {
        const varType = findVariableType(document, position, typeName);
        if (varType) {
            const typeInfo = definitions.types.get(varType);
            if (typeInfo) {
                method = typeInfo.methods.find(m => m.name === methodName);
                container = varType;
            }
        }
    }

    if (!method) return null;

    const sig = new vscode.SignatureInformation(
        `${methodName}(${formatParams(method.params)}): ${method.returnType}`
    );

    sig.parameters = method.params.map(p =>
        new vscode.ParameterInformation(`${p.name}: ${p.type}`)
    );

    const help = new vscode.SignatureHelp();
    help.signatures = [sig];
    help.activeSignature = 0;

    // Count commas to determine active parameter
    const afterParen = lineText.substring(lineText.lastIndexOf('(') + 1);
    help.activeParameter = (afterParen.match(/,/g) || []).length;

    return help;
}

// Helper: find variable type by looking at declarations
function findVariableType(document, position, varName) {
    const text = document.getText();

    // Pattern: Type varName = ... or Type varName;
    const patterns = [
        new RegExp(`(\\w+)\\s+${varName}\\s*=`),
        new RegExp(`(\\w+)\\s+${varName}\\s*;`),
        new RegExp(`mut\\s+(\\w+)\\s+${varName}\\s*=`)
    ];

    for (const pattern of patterns) {
        const match = text.match(pattern);
        if (match) {
            return match[1];
        }
    }

    return null;
}

// Helper: format params for display
function formatParams(params) {
    return params.map(p => `${p.name}: ${p.type}`).join(', ');
}

// Helper: create snippet placeholders
function createSnippetParams(params) {
    return params.map((p, i) => `\${${i + 1}:${p.name}}`).join(', ');
}

function deactivate() { }

module.exports = { activate, deactivate };
